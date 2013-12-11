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

#pragma once

#include "cinder/audio2/Context.h"
#include "cinder/audio2/NodeEffect.h"
#include "cinder/audio2/dsp/RingBuffer.h"
#include "cinder/audio2/cocoa/CinderCoreAudio.h"

#include <AudioUnit/AudioUnit.h>

namespace cinder { namespace audio2 { namespace cocoa {

class DeviceAudioUnit;
class ContextAudioUnit;

class NodeAudioUnit {
  public:
	NodeAudioUnit() : mAudioUnit( nullptr ), mOwnsAudioUnit( true )	{}
	virtual ~NodeAudioUnit();

	virtual ::AudioUnit getAudioUnit() const	{ return mAudioUnit; }

  protected:
	void initAu();
	void uninitAu();

	::AudioUnit			mAudioUnit;
	bool				mOwnsAudioUnit;
	Buffer*				mProcessBuffer;

	struct RenderData {
		Node				*node;
		ContextAudioUnit	*context;
	} mRenderData;
};

class LineOutAudioUnit : public LineOut, public NodeAudioUnit {
  public:
	LineOutAudioUnit( DeviceRef device, const Format &format = Format() );
	virtual ~LineOutAudioUnit() = default;

	std::string virtual getName() override			{ return "LineOutAudioUnit"; }

	void initialize() override;
	void uninitialize() override;

	void start() override;
	void stop() override;

	uint64_t getNumProcessedFrames() override		{ return mProcessedFrames; }
	uint64_t getLastClip() override;

  private:
	bool checkNotClipping();
	static OSStatus renderCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 busNumber, UInt32 numFrames, ::AudioBufferList *bufferList );

	std::atomic<uint64_t>				mProcessedFrames, mLastClip;
	bool								mSynchronousIO;

	friend class LineInAudioUnit;
};

class LineInAudioUnit : public LineIn, public NodeAudioUnit {
  public:
	LineInAudioUnit( DeviceRef device, const Format &format = Format() );
	virtual ~LineInAudioUnit();

	std::string virtual getName() override			{ return "LineInAudioUnit"; }

	void initialize() override;
	void uninitialize() override;

	void start() override;
	void stop() override;

	void process( Buffer *buffer ) override;

	uint64_t getLastUnderrun() override;
	uint64_t getLastOverrun() override;

  private:
	static OSStatus renderCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 bus, UInt32 numFrames, ::AudioBufferList *bufferList );
	static OSStatus inputCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 bus, UInt32 numFrames, ::AudioBufferList *bufferList );

	dsp::RingBuffer						mRingBuffer;
	size_t								mRingBufferPaddingFactor;
	AudioBufferListPtr					mBufferList;
	std::atomic<uint64_t>				mLastUnderrun, mLastOverrun;
	bool								mSynchronousIO;

};

// TODO: when stopped / mEnabled = false; kAudioUnitProperty_BypassEffect should be used
class NodeEffectAudioUnit : public NodeEffect, public NodeAudioUnit {
  public:
	NodeEffectAudioUnit( UInt32 subType, const Format &format = Format() );
	virtual ~NodeEffectAudioUnit();

	std::string virtual getName() override			{ return "NodeEffectAudioUnit"; }

	void initialize() override;
	void uninitialize() override;
	void process( Buffer *buffer ) override;

	void setParameter( ::AudioUnitParameterID paramId, float val );

  private:
	static OSStatus renderCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 busNumber, UInt32 numFrames, ::AudioBufferList *bufferList );

	UInt32		mEffectSubType;
	AudioBufferListPtr mBufferList;
};

class ContextAudioUnit : public Context {
  public:
	virtual ~ContextAudioUnit();

	virtual LineOutRef		createLineOut( const DeviceRef &device, const Node::Format &format = Node::Format() ) override;
	virtual LineInRef		createLineIn( const DeviceRef &device, const Node::Format &format = Node::Format() ) override;

	//! set by the NodeTarget
	void setCurrentTimeStamp( const ::AudioTimeStamp *timeStamp ) { mCurrentTimeStamp = timeStamp; }
	//! all other NodeAudioUnit's need to pass this correctly formatted timestamp to AudioUnitRender
	const ::AudioTimeStamp* getCurrentTimeStamp() { return mCurrentTimeStamp; }

  private:

	const ::AudioTimeStamp *mCurrentTimeStamp;
};

} } } // namespace cinder::audio2::cocoa