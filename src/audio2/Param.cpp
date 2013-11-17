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

#include "audio2/Param.h"
#include "audio2/Debug.h"

using namespace std;

namespace cinder { namespace audio2 {

Param::Event::Event( uint64_t beginFrame, uint64_t endFrame, double totalSeconds, float endValue )
	: mBeginFrame( beginFrame ), mEndFrame( endFrame ), mTotalSeconds( totalSeconds ), mEndValue( endValue )
{
}

void Param::initialize( const ContextRef &context )
{
	mContext = context;
}

void Param::setValue( float value )
{
	mValue = value;
}

void Param::rampTo( float value, double rampSeconds )
{
	CI_ASSERT( mContext );

	if( ! mInternalBufferInitialized )
		mInternalBuffer.resize( mContext->getFramesPerBlock() );

//	size_t framesPerBlock = mContext->getFramesPerBlock();
	size_t sampleRate = mContext->getSampleRate();
	uint64_t rampFrames = rampSeconds * sampleRate;

	uint64_t beginFrame = mContext->getNumProcessedFrames();
	uint64_t endFrame = beginFrame + rampFrames;

	Event event( beginFrame, endFrame, rampSeconds, value );

	float deltaValue = value - mValue;
	event.mIncr = deltaValue / float( endFrame - beginFrame );

	LOG_V << "scheduling event with begin frame: " << event.mBeginFrame << ", endFrame: " << event.mEndFrame << ", rampFrames: " << rampFrames << ", incr: " << event.mIncr << endl;

	lock_guard<mutex> lock( mContext->getMutex() );

	mEvents.resize( 1 );
	mEvents[0] = event;
}

bool Param::isVaryingNextEval() const
{
	CI_ASSERT( mContext );

	if( ! mEvents.empty() ) {
		uint64_t beginFrame = mContext->getNumProcessedFrames();
		uint64_t endFrame = beginFrame + mContext->getFramesPerBlock();

		const Event &event = mEvents[0];

		if( event.mBeginFrame <= beginFrame || event.mEndFrame >= endFrame )
			return true;
	}
	return false;
}

float* Param::getValueArray()
{
	CI_ASSERT( mContext );

	uint64_t beginFrame = mContext->getNumProcessedFrames();
	uint64_t endFrame = beginFrame + mContext->getFramesPerBlock();
	eval( beginFrame, endFrame, mInternalBuffer.data(), mInternalBuffer.size(), mContext->getSampleRate() );

	return mInternalBuffer.data();
}

// FIXME: hearing a pop right before completion
void Param::eval( uint64_t beginFrame, uint64_t endFrame, float *array, size_t arrayLength, size_t sampleRate )
{
	if( mEvents.empty() )
		return;

	const Event &event = mEvents[0];

	uint64_t startRelative = ( beginFrame >= event.mBeginFrame ? 0 : beginFrame - event.mBeginFrame );
	uint64_t endRelative = ( endFrame < event.mEndFrame ? arrayLength : endFrame - event.mEndFrame );

	CI_ASSERT( startRelative <= arrayLength && endRelative <= arrayLength );

	if( startRelative > 0 )
		memset( array, 0, (size_t)startRelative * sizeof( float ) );

	float value = mValue;
	float incr = event.mIncr;
	for( uint64_t i = startRelative; i < endRelative; i++ ) {
		value += incr;
		array[i] = value;
	}

	mValue = value;

	if( endRelative < arrayLength ) {
		size_t zeroLeft = size_t( arrayLength - endRelative );
		size_t offset = (size_t)endRelative;
		memset( array + offset, 0, zeroLeft * sizeof( float ) );
	}

	if( endFrame >= event.mEndFrame )
		mEvents.clear();
}

} } // namespace cinder::audio2