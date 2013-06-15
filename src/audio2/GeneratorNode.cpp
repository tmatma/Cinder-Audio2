#include "audio2/GeneratorNode.h"
#include "audio2/RingBuffer.h"
#include "audio2/Debug.h"

using namespace ci;
using namespace std;

namespace audio2 {

// ----------------------------------------------------------------------------------------------------
// MARK: - GeneratorNode
// ----------------------------------------------------------------------------------------------------

GeneratorNode::GeneratorNode() : Node()
{
	mSources.clear();
	mFormat.setWantsDefaultFormatFromParent();
}

// ----------------------------------------------------------------------------------------------------
// MARK: - BufferPlayerNode
// ----------------------------------------------------------------------------------------------------

BufferPlayerNode::BufferPlayerNode( BufferRef buffer )
: PlayerNode(), mBuffer( buffer )
{
	mTag = "BufferPlayerNode";
	mNumFrames = mBuffer->getNumFrames();
	mFormat.setNumChannels( mBuffer->getNumChannels() );
}

void BufferPlayerNode::start()
{
	CI_ASSERT( mBuffer );

	mReadPos = 0;
	mEnabled = true;

	LOG_V << "started" << endl;
}

void BufferPlayerNode::stop()
{
	mEnabled = false;

	LOG_V << "stopped" << endl;
}

void BufferPlayerNode::process( Buffer *buffer )
{
	size_t readPos = mReadPos;
	size_t numFrames = buffer->getNumFrames();
	size_t readCount = std::min( mNumFrames - readPos, numFrames );

	for( size_t ch = 0; ch < buffer->getNumChannels(); ch++ )
		std::memcpy( buffer->getChannel( ch ), &mBuffer->getChannel( ch )[readPos], readCount * sizeof( float ) );

	if( readCount < numFrames  ) {
		size_t numLeft = numFrames - readCount;
		for( size_t ch = 0; ch < buffer->getNumChannels(); ch++ )
			std::memset( &buffer->getChannel( ch )[readCount], 0, numLeft * sizeof( float ) );

		if( mLoop ) {
			mReadPos = 0;
			return;
		} else
			mEnabled = false;
	}

	mReadPos += readCount;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - FilePlayerNode
// ----------------------------------------------------------------------------------------------------


FilePlayerNode::FilePlayerNode()
: PlayerNode(), mNumFramesBuffered( 0 )
{
}

FilePlayerNode::~FilePlayerNode()
{
}

FilePlayerNode::FilePlayerNode( SourceFileRef sourceFile )
: PlayerNode(), mSourceFile( sourceFile ), mNumFramesBuffered( 0 )
{
	mTag = "FilePlayerNode";
	mNumFrames = mSourceFile->getNumFrames();
	mBufferFramesThreshold = 1024; // TODO: expose
}

void FilePlayerNode::initialize()
{
	mSourceFile->setNumChannels( mFormat.getNumChannels() );
	mSourceFile->setSampleRate( mFormat.getSampleRate() );

	// FIXME: numFramesPerRead > block size is broken
	mSourceFile->setNumFramesPerRead( 512 );
	size_t paddingMultiplier = 2;

	mReadBuffer = Buffer( mFormat.getNumChannels(), mSourceFile->getNumFramesPerRead() );
	mRingBuffer = unique_ptr<RingBuffer>( new RingBuffer( mFormat.getNumChannels() * mSourceFile->getNumFramesPerRead() * paddingMultiplier ) );
}

void FilePlayerNode::start()
{
	CI_ASSERT( mSourceFile );

	mSourceFile->seek( 0 );
	mReadPos = 0;
	mEnabled = true;

	LOG_V << "started" << endl;
}

void FilePlayerNode::stop()
{
	mEnabled = false;

	LOG_V << "stopped" << endl;
}

void FilePlayerNode::process( Buffer *buffer )
{
	size_t numFrames = buffer->getNumFrames();
	readFile( numFrames );

	size_t readCount = std::min( mNumFramesBuffered, numFrames );

	for( size_t ch = 0; ch < buffer->getNumChannels(); ch++ ) {
		size_t count = mRingBuffer->read( buffer->getChannel( ch ), readCount );
		if( count != numFrames )
			LOG_V << " Warning, unexpected read count: " << count << ", expected: " << numFrames << " (ch = " << ch << ")" << endl;
	}
	mNumFramesBuffered -= readCount;

	// check if end of file
	if( readCount < numFrames  ) {
		size_t numLeft = numFrames - readCount;
		
		// TODO: move this memset to method on buffer, use also in BufferPlayerNode
		for( size_t ch = 0; ch < buffer->getNumChannels(); ch++ )
			std::memset( &buffer->getChannel( ch )[readCount], 0, numLeft * sizeof( float ) );

		if( mLoop ) {
			mReadPos = 0;
			return;
		} else
			mEnabled = false;
	}
}

// FIXME: this copy is really janky
// - mReadBuffer and mRingBuffer are much bigger than the block size
// - since they are non-interleaved, need to pack them in sections = numFramesPerBlock so stereo channels can be
//   pulled out appropriately.
// - Ideally, there would only be one buffer copied to on the background thread and then one copy/consume in process()	

void FilePlayerNode::readFile( size_t numFramesPerBlock )
{
	size_t readPos = mReadPos;

	if( mNumFramesBuffered > mBufferFramesThreshold || readPos >= mNumFrames )
		return;

	size_t numRead = mSourceFile->read( &mReadBuffer, readPos );

//	app::console() << "BUFFER (" << numRead << ")" << endl;
	while( numRead > 0 ) {
		size_t writeCount = std::min( numFramesPerBlock, numRead );
		for( size_t ch = 0; ch < mReadBuffer.getNumChannels(); ch++ )
			mRingBuffer->write( mReadBuffer.getChannel( ch ), numFramesPerBlock );

		numRead -= writeCount;
		CI_ASSERT( numRead < mReadBuffer.getSize() );
		mNumFramesBuffered += writeCount;
		mReadPos += writeCount;
	}
}

} // namespace audio2