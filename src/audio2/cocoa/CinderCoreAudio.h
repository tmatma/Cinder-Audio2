#pragma once

#include "audio2/audio.h"

#include <memory>
#include <AudioToolbox/AudioToolbox.h>

struct AudioStreamBasicDescription;

namespace audio2 { namespace cocoa {

class ConverterCoreAudio : public Converter {
public:
	ConverterCoreAudio( const Format &sourceFormat, const Format &destFormat );
	virtual ~ConverterCoreAudio();

	virtual void convert( Buffer *sourceBuffer, Buffer *destBuffer ) override;

private:
	::AudioConverterRef mAudioConverter;
};

//! convience function for pretty printing \a asbd
void printASBD( const ::AudioStreamBasicDescription &asbd );

struct AudioBufferListDeleter {
	void operator()( ::AudioBufferList *bufferList ) { free( bufferList ); }
};

typedef std::unique_ptr<::AudioBufferList, AudioBufferListDeleter> AudioBufferListRef;

// TODO: consider adopting the CAPublicUitility way of doing this (I think it does it on the stack)
AudioBufferListRef createNonInterleavedBufferList( size_t numChannels, size_t numFrames );

::AudioComponent findAudioComponent( const ::AudioComponentDescription &componentDescription );
void findAndCreateAudioComponent( const ::AudioComponentDescription &componentDescription, ::AudioComponentInstance *componentInstance );

::AudioStreamBasicDescription createFloatAsbd( size_t numChannels, size_t sampleRate, bool isInterleaved = false );

inline void copyToGenericBuffer( ::AudioBufferList *bufferList, Buffer *buffer )
{
}

} } // namespace audio2::cocoa