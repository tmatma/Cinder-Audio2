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

#include "audio2/Buffer.h"
#include "audio2/Dsp.h"

#include "cinder/Cinder.h"

#include <vector>

#if defined( CINDER_AUDIO_VDSP )
	#include <Accelerate/Accelerate.h>
#else
	#define CINDER_AUDIO_OOURA
#endif

namespace cinder { namespace audio2 {

class Fft {
public:
	Fft( size_t fftSize = 512 );
	~Fft();

	void compute( Buffer *buffer );

	// TODO: implement
	void computeInverse() {}

	size_t getSize() const	{ return mSize; }

	std::vector<float>& getReal()	{ return mReal; }
	std::vector<float>& getImag()	{ return mImag; }

	const std::vector<float>& getReal() const	{ return mReal; }
	const std::vector<float>& getImag()	const	{ return mImag; }

protected:
	std::vector<float> mReal, mImag;
	size_t mSize;

#if defined( CINDER_AUDIO_VDSP )
	size_t mLog2FftSize;
	::FFTSetup mFftSetup;
	::DSPSplitComplex mSplitComplexFrame;
#elif defined( CINDER_AUDIO_OOURA )
	int *mOouraIp;
	float *mOouraW;
#endif

};

} } // namespace cinder::audio2