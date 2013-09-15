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

#include "audio2/audio.h"
#include "audio2/File.h"
#include "audio2/Dsp.h"
#include "audio2/Atomic.h"

#include "cinder/DataSource.h"
#include "cinder/Thread.h"

namespace cinder { namespace audio2 {

typedef std::shared_ptr<class NodeSamplePlayer>		PlayerNodeRef;
typedef std::shared_ptr<class NodeBufferPlayer> BufferPlayerNodeRef;
typedef std::shared_ptr<class NodeFilePlayer>	FilePlayerNodeRef;

class RingBuffer;

class NodeSource : public Node {
  public:
	std::string virtual getTag() override			{ return "NodeSource"; }
	virtual ~NodeSource() {}

  protected:
	NodeSource( const Format &format );
  private:
	// NodeSource's cannot have any sources
	void setInput( const NodeRef &input ) override {}
	void setInput( const NodeRef &input, size_t bus ) override {}
};

class NodeLineIn : public NodeSource {
public:
	virtual ~NodeLineIn() {}

	std::string virtual getTag() override			{ return "NodeLineIn"; }

	//! Returns the associated \a Device.
	virtual const DeviceRef& getDevice() const		{ return mDevice; }
	//! Returns the frame of the last buffer underrun or 0 if none since the last time this method was called.
	virtual uint64_t getLastUnderrun() = 0;
	//! Returns the frame of the last buffer overrun or 0 if none since the last time this method was called.
	virtual uint64_t getLastOverrun() = 0;

protected:
	NodeLineIn( const DeviceRef &device, const Format &format );

	DeviceRef mDevice;
};

//! \brief Base Node class for sampled audio playback
//! \note NodeSamplePlayer itself doesn't process any audio, but contains the common interface for Node's that do.
//! \see NodeBufferPlayer
//! \see NodeFilePlayer
class NodeSamplePlayer : public NodeSource {
public:
	std::string virtual getTag() override			{ return "NodeSamplePlayer"; }

	virtual void setReadPosition( size_t pos )	{ mReadPos = pos; }
	virtual size_t getReadPosition() const	{ return mReadPos; }

	virtual void setLoop( bool b = true )	{ mLoop = b; }
	virtual bool getLoop() const			{ return mLoop; }

	virtual size_t getNumFrames() const	{ return mNumFrames; }

protected:
	NodeSamplePlayer( const Format &format = Format() ) : NodeSource( format ), mNumFrames( 0 ), mReadPos( 0 ), mLoop( false ) {}
	virtual ~NodeSamplePlayer() {}

	size_t mNumFrames;
	std::atomic<size_t> mReadPos;
	std::atomic<bool>	mLoop;
};

class NodeBufferPlayer : public NodeSamplePlayer {
public:
	NodeBufferPlayer( const Format &format = Format() );
	NodeBufferPlayer( const BufferRef &buffer, const Format &format = Format() );
	virtual ~NodeBufferPlayer() {}

	std::string virtual getTag() override			{ return "NodeBufferPlayer"; }

	virtual void start() override;
	virtual void stop() override;
	virtual void process( Buffer *buffer );

	BufferRef getBuffer() const	{ return mBuffer; }
	void setBuffer( const BufferRef &buffer );

protected:
	BufferRef mBuffer;
};

// TODO: use a thread pool to keep the overrall number of read threads to a minimum.
class NodeFilePlayer : public NodeSamplePlayer {
public:
	NodeFilePlayer( const Format &format = Format() );
	NodeFilePlayer( const SourceFileRef &sourceFile, bool isMultiThreaded = true, const Format &format = Node::Format() );
	virtual ~NodeFilePlayer();

	std::string virtual getTag() override			{ return "NodeFilePlayer"; }

	void initialize() override;

	virtual void start() override;
	virtual void stop() override;
	virtual void process( Buffer *buffer ) override;

	virtual void setReadPosition( size_t pos ) override;

	bool isMultiThreaded() const	{ return mMultiThreaded; }

  protected:

	void readFromBackgroundThread();
	void readFile();
	bool moreFramesNeeded();

	std::unique_ptr<std::thread> mReadThread;
	std::unique_ptr<RingBuffer> mRingBuffer;
	Buffer mReadBuffer;
	size_t mNumFramesBuffered;

	SourceFileRef mSourceFile;
	size_t mBufferFramesThreshold;
	size_t mSampleRate;
	bool mMultiThreaded;
	std::atomic<bool> mReadOnBackground;

	std::atomic<size_t> mFramesPerBlock;
};

// TODO: NodeGen's are starting to seem unnesecarry
// - just make a NodeSource for all of the basic waveforms
template <typename GenT>
struct NodeGen : public NodeSource {
	NodeGen( const Format &format = Format() ) : NodeSource( format )
	{
		mChannelMode = ChannelMode::SPECIFIED;
		setNumChannels( 1 );
	}

	std::string virtual getTag() override			{ return "NodeGen"; }

	virtual void initialize() override
	{
		size_t newSampleRate = getContext()->getSampleRate();
		mGen.setSampleRate( getContext()->getSampleRate() );
	}

	virtual void process( Buffer *buffer ) override
	{
		size_t count = buffer->getNumFrames();
		mGen.process( buffer->getChannel( 0 ), count );
	}

	GenT& getGen()				{ return mGen; }
	const GenT& getGen() const	{ return mGen; }

protected:
	GenT mGen;
};

} } // namespace cinder::audio2