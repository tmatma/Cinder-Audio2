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

#include "cinder/audio2/FileOggVorbis.h"
#include "cinder/audio2/dsp/Converter.h"
#include "cinder/audio2/Exception.h"
#include "cinder/audio2/Debug.h"

#include <sstream>

using namespace std;

namespace cinder { namespace audio2 {

SourceFileImplOggVorbis::SourceFileImplOggVorbis( const DataSourceRef &dataSource )
	: SourceFile( dataSource ), mReadPos( 0 )
{
	int status = ov_fopen( dataSource->getFilePath().string().c_str(), &mOggVorbisFile );
	if( status )
		throw AudioFileExc( string( "Failed to open Ogg Vorbis file with error: " ), (int32_t)status );

	vorbis_info *info = ov_info( &mOggVorbisFile, -1 );
    mSampleRate = mNativeSampleRate = info->rate;
    mNumChannels = mNativeNumChannels = info->channels;

	ogg_int64_t totalFrames = ov_pcm_total( &mOggVorbisFile, -1 );
    mNumFrames = mFileNumFrames = static_cast<uint32_t>( totalFrames );
}

SourceFileImplOggVorbis::~SourceFileImplOggVorbis()
{
	ov_clear( &mOggVorbisFile );
}

void SourceFileImplOggVorbis::outputFormatUpdated()
{
	if( mSampleRate != mNativeSampleRate || mNumChannels != mNativeNumChannels ) {
		mConverter = audio2::dsp::Converter::create( mNativeSampleRate, mSampleRate, mNativeNumChannels, mNumChannels, mMaxFramesPerRead );
		mNumFrames = std::ceil( (float)mFileNumFrames * (float)mSampleRate / (float)mNativeSampleRate );

		LOG_V( "created conveter for samplerate: " << mNativeSampleRate << " -> " << mSampleRate << ", channels: " << mNativeNumChannels << " -> " << mNumChannels << ", output num frames: " << mNumFrames );
	}
	else {
		mNumFrames = mFileNumFrames;
		mConverter.reset();
	}
}

size_t SourceFileImplOggVorbis::read( Buffer *buffer )
{
	CI_ASSERT( buffer->getNumChannels() == mNumChannels );
	CI_ASSERT( mReadPos < mNumFrames ); // FIXME: this failed while seeking and no conversion, synchronious I/O

	if( mConverter )
		return readImplConvert( buffer );

	return readImpl( buffer );
}

BufferRef SourceFileImplOggVorbis::loadBuffer()
{
	if( mReadPos != 0 )
		seek( 0 );

	if( mConverter )
		return loadBufferImplConvert();

	return loadBufferImpl();
}

long SourceFileImplOggVorbis::readIntoBufferImpl( Buffer *buffer, size_t offset, size_t length )
{
	float **outChannels;
	int section;

	long outNumFrames = ov_read_float( &mOggVorbisFile, &outChannels, (int)length, &section );
	if( outNumFrames < 0 )
		throw AudioFileExc( "ov_read_float error", (int32_t)outNumFrames );

	for( int ch = 0; ch < mNumChannels; ch++ ) {
		float *channel = outChannels[ch];
		copy( channel, channel + outNumFrames, buffer->getChannel( ch ) + offset );
	}

	return outNumFrames;
}

size_t SourceFileImplOggVorbis::readImpl( Buffer *buffer )
{
	size_t framesLeft = mNumFrames - mReadPos;
	int numReadFramesNeeded = (int)std::min( framesLeft, std::min( mMaxFramesPerRead, buffer->getNumFrames() ) );
	int readCount = 0;

	while( readCount < numReadFramesNeeded ) {
		long outNumFrames = readIntoBufferImpl( buffer, readCount, numReadFramesNeeded - readCount );
		if( outNumFrames == 0 )
			break;

		readCount += outNumFrames;
	}

	mReadPos += readCount;
	return readCount;
}

size_t SourceFileImplOggVorbis::readImplConvert( Buffer *buffer )
{
	size_t sourceBufFrames = buffer->getNumFrames() * (float)mNativeSampleRate / (float)mSampleRate;
	int numReadFramesNeeded = (int)std::min( mFileNumFrames - mReadPos, std::min( mMaxFramesPerRead, sourceBufFrames ) );

	int readCount = 0;

	BufferDynamic sourceBuffer( mMaxFramesPerRead, mNativeNumChannels );

	while( readCount < numReadFramesNeeded ) {
		long outNumFrames = readIntoBufferImpl( &sourceBuffer, readCount, numReadFramesNeeded - readCount );
		if( outNumFrames == 0 )
			break;

		readCount += outNumFrames;
	}

	if( numReadFramesNeeded != sourceBuffer.getNumFrames() )
		sourceBuffer.setNumFrames( numReadFramesNeeded );

	pair<size_t, size_t> count = mConverter->convert( &sourceBuffer, buffer );

	for( int ch = 0; ch < mNumChannels; ch++ ) {
		float *channel = buffer->getChannel( ch );
		copy( channel, channel + count.second, buffer->getChannel( ch ) );
	}

	mReadPos += count.second;
	return count.second;
}

BufferRef SourceFileImplOggVorbis::loadBufferImpl()
{
	BufferRef result( new Buffer( mNumFrames, mNumChannels ) );

	while( true ) {
        float **outChannels;
		int section;
        long outNumFrames = ov_read_float( &mOggVorbisFile, &outChannels, (int)mMaxFramesPerRead, &section );
        if( outNumFrames <= 0 ) {
			if( outNumFrames < 0 )
				LOG_E( "stream error." );
            break;
		}
        else {
            for( int ch = 0; ch < mNumChannels; ch++ ) {
				float *channel = outChannels[ch];
				copy( channel, channel + outNumFrames, result->getChannel( ch ) + mReadPos );
            }
            mReadPos += outNumFrames;
		}
	}

	return result;
}

// TODO: need BufferView's in order to reduce number of copies
BufferRef SourceFileImplOggVorbis::loadBufferImplConvert()
{
	BufferDynamic sourceBuffer( mMaxFramesPerRead, mNativeNumChannels ); // TODO: move this to ivar? it is used in read() as well, when there is a converter
	BufferRef destBuffer( new Buffer( mConverter->getDestMaxFramesPerBlock(), mNumChannels ) );
	BufferRef result( new Buffer( mNumFrames, mNumChannels ) );

	while( true ) {
        float **outChannels;
		int section;
        long outNumFrames = ov_read_float( &mOggVorbisFile, &outChannels, (int)mMaxFramesPerRead, &section );
        if( outNumFrames <= 0 ) {
			if( outNumFrames < 0 )
				LOG_E( "stream error." );
            break;
		}
        else {
            for( int ch = 0; ch < mNumChannels; ch++ ) {
				float *channel = outChannels[ch];
				copy( channel, channel + outNumFrames, sourceBuffer.getChannel( ch ) );
            }

			if( outNumFrames != sourceBuffer.getNumFrames() )
				sourceBuffer.setNumFrames( outNumFrames );
			
			pair<size_t, size_t> count = mConverter->convert( &sourceBuffer, destBuffer.get() );

            for( int ch = 0; ch < mNumChannels; ch++ ) {
				float *channel = destBuffer->getChannel( ch );
				copy( channel, channel + count.second, result->getChannel( ch ) + mReadPos );
            }

            mReadPos += count.second;
		}
	}

	return result;
}

void SourceFileImplOggVorbis::seek( size_t readPositionFrames )
{
	if( readPositionFrames >= mNumFrames )
		return;

	// TODO: convert to native read pos if necessary

	int status = ov_pcm_seek( &mOggVorbisFile, (ogg_int64_t)readPositionFrames );
	CI_ASSERT( ! status );

	mReadPos = readPositionFrames;
}

string SourceFileImplOggVorbis::getMetaData() const
{
	ostringstream str;
	const auto vf = const_cast<OggVorbis_File *>( &mOggVorbisFile );

	str << "encoded by: " << ov_comment( vf, -1 )->vendor << endl;
	str << "comments: " << endl;
	char **comment = ov_comment( vf, -1 )->user_comments;
	while( *comment )
		str << *comment++ << endl;

	return str.str();
}

} } // namespace cinder::audio2