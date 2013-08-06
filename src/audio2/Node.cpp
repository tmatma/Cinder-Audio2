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
#include "audio2/Dsp.h"
#include "audio2/Debug.h"
#include "audio2/CinderAssert.h"

#include "cinder/Utilities.h"

using namespace std;

namespace audio2 {

// ----------------------------------------------------------------------------------------------------
// MARK: - Node
// ----------------------------------------------------------------------------------------------------

// TODO: seems fitting to pass in Context* as first argument to all Node's
// - setting during init no longer necessary
// - provides samplerate / num frames at init

Node::Node( const Format &format )
: mInitialized( false ), mEnabled( false ),	mWantsDefaultFormatFromOutput( format.getWantsDefaultFormatFromOutput() ),
mNumChannels( 1 ), mNumChannelsUnspecified( true ), mBufferLayout( Buffer::Layout::NonInterleaved ), mAutoEnabled( false ), mProcessInPlace( true )
{
	if( format.getChannels() ) {
		mNumChannels = format.getChannels();
		mNumChannelsUnspecified = false;
	}
}

Node::~Node()
{

}

void Node::initialize()
{
	// TODO: this should be created at the end of connect, only if it needs to be based on inputs
	// - at which point, subclasses don't need to call Node::initialize() in their initialize()
	mInternalBuffer = Buffer( mNumChannels, getContext()->getNumFramesPerBlock() );
	mInitialized = true;
}

NodeRef Node::connect( NodeRef dest )
{
	dest->setInput( shared_from_this() );
	return dest;
}

NodeRef Node::connect( NodeRef dest, size_t bus )
{
	dest->setInput( shared_from_this(), bus );
	return dest;
}

// TODO: need 2 variants
// - if multi-output is supported, use getOutput( bus )->getInputs()
void Node::disconnect( size_t bus )
{
	stop();

	mInputs.clear();

	auto output = getOutput();
	if( output ) {
		auto& parentInputs = output->getInputs();
		for( size_t i = 0; i < parentInputs.size(); i++ ) {
			if( parentInputs[i] == shared_from_this() )
				parentInputs[i].reset();
		}
		//		mOutput.reset();
		mOutput = std::weak_ptr<Node>();
	}
}

void Node::setInput( NodeRef input )
{
	if( ! checkInput( input ) )
		return;

	input->setOutput( shared_from_this() );

	// find first available slot
	for( size_t i = 0; i < mInputs.size(); i++ ) {
		if( ! mInputs[i] ) {
			mInputs[i] = input;
			configureProcessing();
			return;
		}
	}

	// or append
	mInputs.push_back( input );
	configureProcessing();
}


// TODO: figure out how to best handle node replacements
void Node::setInput( NodeRef input, size_t bus )
{
	if( ! checkInput( input ) )
		return;

	if( bus > mInputs.size() )
		throw AudioExc( string( "bus " ) + ci::toString( bus ) + " is out of range (max: " + ci::toString( mInputs.size() ) + ")" );
	if( mInputs[bus] )
		throw AudioExc(  string( "bus " ) + ci::toString( bus ) + " is already in use. Replacing busses not yet supported." );

	mInputs[bus] = input;
	input->setOutput( shared_from_this() );
	configureProcessing();
}

bool Node::isConnectedToInput( const NodeRef &input ) const
{
	return find( mInputs.begin(), mInputs.end(), input ) != mInputs.end();
}

bool Node::isConnectedToOutput( const NodeRef &output ) const
{
	return ( getOutput() == output );
}

void Node::fillFormatParamsFromOutput()
{
	NodeRef output = getOutput();
	CI_ASSERT( output );

	while( output && ! mNumChannels ) {
		fillFormatParamsFromNode( output );
		output = output->getOutput();
	}

	CI_ASSERT( mNumChannels );
}

// TODO: consider using a map<index, NodeRef> instead of vector to hold nodes
//	- pro: I can remove it from the map and the Node's bus remains
//  - pro: no more empty slots
void Node::fillFormatParamsFromInput()
{
	for( auto &input : mInputs ) {
		if( input )
			fillFormatParamsFromNode( input );
	}

	CI_ASSERT( mNumChannels );
}

void Node::setEnabled( bool enabled )
{
	if( enabled )
		start();
	else
		stop();
}

// FIXME: mUseSummingBuffer is not thread-safe
// - probably high-time to ditch atomic<bool> and go with std::mutex, since there are multiple pieces that need to be synchronized
void Node::pullInputs( Buffer *outputBuffer )
{
	if( mProcessInPlace ) {
		for( NodeRef &input : mInputs ) {
			if( ! input )
				continue;

			input->pullInputs( outputBuffer );
		}

		if( mEnabled )
			process( outputBuffer );

		return;
	}

	CI_ASSERT( mInternalBuffer.getNumChannels() == outputBuffer->getNumChannels() );


	mInternalBuffer.zero();
	outputBuffer->zero();

	for( NodeRef &input : mInputs ) {
		if( ! input )
			continue;

		// FIXME: inPlaceBuffer is not suitable here when it is a differnt channel count than mInternalBuffer

//		input->pullInputs( outputBuffer );
//		if( input->getProcessInPlace() )
//			sumToInternalBuffer( outputBuffer );
//		else
//			sumToInternalBuffer( input->getInternalBuffer() );

		input->pullInputs( &mInternalBuffer );
		sumInternalToOutput( outputBuffer );
//		if( input->getProcessInPlace() )
//			sumToInternalBuffer(  );
//		else
//			sumToInternalBuffer( input->getInternalBuffer() );

	}

	// FIXME NEXT: mInternalBuffer below no longer has accumulated samples, it just has the last input while summing went to outputBuffer

	if( mEnabled )
		process( &mInternalBuffer );

//	memcpy( outputBuffer->getData(), mInternalBuffer.getData(), outputBuffer->getSize() * sizeof( float ) );
}

size_t Node::getNumInputs() const
{
	size_t result = 0;
	for( const auto &input : mInputs ) {
		if( input )
			result++;
	}

	return result;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - Protected
// ----------------------------------------------------------------------------------------------------

void Node::fillFormatParamsFromNode( const NodeRef &otherNode )
{
	mNumChannels = otherNode->getNumChannels();
}

void Node::configureProcessing()
{
	if( getNumInputs() > 1 ) {
		mProcessInPlace = false;
		return;
	}

	for( auto &input : mInputs ) {
		if( input && input->getNumChannels() != mNumChannels ) {
			mProcessInPlace = false;
			return;
		}
	}

	mProcessInPlace = true;
}

void Node::sumToInternalBuffer( const Buffer *buffer )
{
	CI_ASSERT( ! mProcessInPlace );

	size_t bufferChannels = buffer->getNumChannels();
	if( mNumChannels == bufferChannels ) {
		for( size_t c = 0; c < bufferChannels; c++ )
			sum( buffer->getChannel( c ), mInternalBuffer.getChannel( c ), mInternalBuffer.getChannel( c ), mInternalBuffer.getNumFrames() );
	}
	else if( bufferChannels == 1 ) {
		// up-mix mono input to all of this Node's channels
		for( size_t c = 0; c < mNumChannels; c++ )
			sum( buffer->getChannel( 0 ), mInternalBuffer.getChannel( c ), mInternalBuffer.getChannel( c ), mInternalBuffer.getNumFrames() );
	}
	else
		CI_ASSERT( 0 && "unhandled" );
}

// TODO: handle downmix from stereo to mono
void Node::sumInternalToOutput( Buffer *outputBuffer )
{
	CI_ASSERT( ! mProcessInPlace );

	size_t bufferChannels = outputBuffer->getNumChannels();
	if( mNumChannels == bufferChannels ) {
		for( size_t c = 0; c < bufferChannels; c++ )
			sum( outputBuffer->getChannel( c ), mInternalBuffer.getChannel( c ), outputBuffer->getChannel( c ), mInternalBuffer.getNumFrames() );
	}
	else if( mNumChannels == 1 ) {
		// up-mix mono input to all of this Node's channels
		for( size_t c = 0; c < mNumChannels; c++ )
			sum( outputBuffer->getChannel( 0 ), mInternalBuffer.getChannel( c ), outputBuffer->getChannel( c ), mInternalBuffer.getNumFrames() );
	}
	else
		CI_ASSERT( 0 && "unhandled" );
}

bool Node::checkInput( const NodeRef &input )
{
	return ( input && ( input != shared_from_this() ) && ! isConnectedToInput( input ) );
}


} // namespace audio2
