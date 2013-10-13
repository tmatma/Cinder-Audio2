/*
 Copyright (c) 2013, The Cinder Project

 This code is intended to be used with the Cinder C++ library, http://libcinder.org

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and
 the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 the following disclaimer in the documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */


#include "audio2/Node.h"
#include "audio2/audio.h"
#include "audio2/Converter.h"
#include "audio2/Dsp.h"
#include "audio2/Debug.h"
#include "audio2/CinderAssert.h"

#include "cinder/Utilities.h"

using namespace std;

namespace cinder { namespace audio2 {

// ----------------------------------------------------------------------------------------------------
// MARK: - Node
// ----------------------------------------------------------------------------------------------------

Node::Node( const Format &format )
	: mInitialized( false ), mEnabled( false ),	mChannelMode( format.getChannelMode() ),
		mNumChannels( 1 ), mAutoEnabled( false ), mProcessInPlace( true )
{
	if( format.getChannels() ) {
		mNumChannels = format.getChannels();
		mChannelMode = ChannelMode::SPECIFIED;
	}

	if( ! boost::indeterminate( format.getAutoEnable() ) )
		setAutoEnabled( format.getAutoEnable() );
}

Node::~Node()
{
}

void Node::disconnect( size_t bus )
{
	lock_guard<mutex> lock( getContext()->getMutex() );

	mInputs.clear();

//	auto output = getOutput();
//	if( output ) {
//		// note: the output must be reset before resetting the output's reference to this Node,
//		// since that may cause this Node to be deallocated.
//		mOutput.reset();
//		
//		auto& parentInputs = output->getInputs();
//		for( size_t i = 0; i < parentInputs.size(); i++ ) {
//			if( parentInputs[i] == shared_from_this() )
//				parentInputs[i].reset();
//		}
//	}

	if( ! mOutputs.empty() ) {

		// hold on to a reference of ourself, since clearing outputs may deallocate us along the way.
		NodeRef self = shared_from_this();


		for( auto& outputWeak : getOutputs() ) {
			auto output = outputWeak.lock();
			if( output ) {
				// note: the output must be reset before resetting the output's reference to this Node,
				// since that may cause this Node to be deallocated.
				mOutput.reset();

				auto& parentInputs = output->getInputs();
				for( size_t i = 0; i < parentInputs.size(); i++ ) {
					if( parentInputs[i] == shared_from_this() )
						parentInputs[i].reset();
				}
			}
		}

		mOutputs.clear();
	}
}

void Node::addInput( const NodeRef &input )
{
	setInput( input, getFirstAvailableBus() );
}

void Node::setInput( const NodeRef &input, size_t bus )
{
	if( ! checkInput( input ) )
		return;


	// TODO: this disconnection kills nodes that are solely owned by the graph. but make sure not disconnecting works out.
	//NodeRef& existingInput = mInputs[bus];
	//if( existingInput )
	//	existingInput->disconnect();

	{
		lock_guard<mutex> lock( getContext()->getMutex() );

		mInputs[bus] = input;
		input->getOutputs().insert( make_pair( bus, shared_from_this() ) );

		configureConnections();
	}

	// must call once lock has been released
	getContext()->connectionsDidChange( shared_from_this() );
}

void Node::pullInputs( Buffer *destBuffer )
{
	CI_ASSERT( getContext() );

	if( mProcessInPlace ) {
		for( auto &in : mInputs )
			in.second->pullInputs( destBuffer );

		if( mEnabled )
			process( destBuffer );
	}
	else {
		mInternalBuffer.zero();
		mSummingBuffer.zero();

		for( auto &in : mInputs ) {
			NodeRef &input = in.second;

			input->pullInputs( &mInternalBuffer );
			if( input->getProcessInPlace() )
				Converter::sumBuffers( &mInternalBuffer, &mSummingBuffer );
			else
				Converter::sumBuffers( input->getInternalBuffer(), &mSummingBuffer );
		}

		if( mEnabled )
			process( &mSummingBuffer );

		Converter::mixBuffers( &mSummingBuffer, destBuffer );
	}
}

void Node::setEnabled( bool enabled )
{
	if( enabled )
		start();
	else
		stop();
}

size_t Node::getNumInputs() const
{
	mInputs.size();
}

// ----------------------------------------------------------------------------------------------------
// MARK: - Protected
// ----------------------------------------------------------------------------------------------------

void Node::initializeImpl()
{
	if( mInitialized )
		return;

	initialize();
	mInitialized = true;
	LOG_V << getTag() << " initialized." << endl;

	if( mAutoEnabled )
		start();
}


void Node::uninitializeImpl()
{
	if( ! mInitialized )
		return;

	if( mAutoEnabled )
		stop();

	uninitialize();
	mInitialized = false;
	LOG_V << getTag() << " un-initialized." << endl;
}

void Node::setNumChannels( size_t numChannels )
{
	if( mNumChannels == numChannels )
		return;

	uninitializeImpl();
	mNumChannels = numChannels;
}

size_t Node::getMaxNumInputChannels() const
{
	size_t result = 0;
	for( auto &in : mInputs )
		result = max( result, in.second->getNumChannels() );

	return result;
}

void Node::configureConnections()
{
	CI_ASSERT( getContext() );

	mProcessInPlace = true;

	if( getNumInputs() > 1 )
		mProcessInPlace = false;

	for( auto &in : mInputs ) {
		auto input = in.second;

		size_t inputNumChannels = input->getNumChannels();
		if( ! supportsInputNumChannels( inputNumChannels ) ) {
			if( mChannelMode == ChannelMode::MATCHES_INPUT )
				setNumChannels( getMaxNumInputChannels() );
			else if( input->getChannelMode() == ChannelMode::MATCHES_OUTPUT ) {
				input->setNumChannels( mNumChannels );
				input->configureConnections();
			}
			else {
				mProcessInPlace = false;
				input->setupProcessWithSumming();
			}
		}

		input->initializeImpl();
	}

	NodeRef output = getOutput();
	if( output && ! output->supportsInputNumChannels( mNumChannels ) ) {
		if( output->getChannelMode() == ChannelMode::MATCHES_INPUT ) {
			output->setNumChannels( mNumChannels );
			output->configureConnections();
		}
		else
			mProcessInPlace = false;
	}

	if( ! mProcessInPlace )
		setupProcessWithSumming();

	initializeImpl();
}

void Node::setupProcessWithSumming()
{
	CI_ASSERT( getContext() );

	mProcessInPlace = false;
	size_t framesPerBlock = getContext()->getFramesPerBlock();

	mInternalBuffer.setSize( framesPerBlock, mNumChannels );
	mSummingBuffer.setSize( framesPerBlock, mNumChannels );
}

bool Node::checkInput( const NodeRef &input )
{
	if( ! input || input == shared_from_this() )
		return false;

	for( const auto& in : mInputs )
		if( input == in.second )
			return false;

	return true;
}

size_t Node::getFirstAvailableBus()
{
	size_t result = 0;
	for( const auto& input : mInputs ) {
		if( input.first != result )
			break;

		result++;
	}

	return result;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - NodeAutoPullable
// ----------------------------------------------------------------------------------------------------

NodeAutoPullable::NodeAutoPullable( const Format &format )
	: Node( format ), mIsPulledByContext( false )
{
}

const NodeRef& NodeAutoPullable::connect( const NodeRef &dest )
{
	if( mIsPulledByContext && dest ) {
		mIsPulledByContext = false;
		getContext()->removeAutoPulledNode( shared_from_this() );
		LOG_V << "removed " << getTag() << " from auto-pull list" << endl;
	}

	return Node::connect( dest );
}

const NodeRef& NodeAutoPullable::connect( const NodeRef &dest, size_t bus )
{
	if( mIsPulledByContext && dest ) {
		mIsPulledByContext = false;
		getContext()->removeAutoPulledNode( shared_from_this() );
		LOG_V << "removed " << getTag() << " from auto-pull list" << endl;
	}

	return Node::connect( dest, bus );
}

void NodeAutoPullable::addInput( const NodeRef &input )
{
	Node::addInput( input );

	updatePullMethod();
}

void NodeAutoPullable::setInput( const NodeRef &input, size_t bus )
{
	Node::setInput( input, bus );
	
	updatePullMethod();
}

void NodeAutoPullable::disconnect( size_t bus )
{
	if( mIsPulledByContext ) {
		mIsPulledByContext = false;
		getContext()->removeAutoPulledNode( shared_from_this() );
		LOG_V << "removed " << getTag() << " from auto-pull list" << endl;
	}

	Node::disconnect( bus );
}

void NodeAutoPullable::updatePullMethod()
{
	auto output = getOutput();
	if( ! output && ! mIsPulledByContext ) {
		mIsPulledByContext = true;
		getContext()->addAutoPulledNode( shared_from_this() );
		LOG_V << "added " << getTag() << " to auto-pull list" << endl;
	}
	else if( output && mIsPulledByContext ) {
		mIsPulledByContext = false;
		getContext()->removeAutoPulledNode( shared_from_this() );
		LOG_V << "removed " << getTag() << " from auto-pull list" << endl;
	}
}

} } // namespace cinder::audio2
