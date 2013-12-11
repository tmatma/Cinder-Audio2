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

#include "cinder/CinderMath.h"

namespace cinder { namespace audio2 {

// TODO: decide on decibel convensions
//		- these match pd but that may not be very general
//! linear gain equal to -100db
const float kGainNegative100Decibels = 0.00001f;
const float kGainNegative100DecibelsInverse = 1.0f / kGainNegative100Decibels;

//! Scale \a gainLinear from linear (0-1) to decibel (0-100) scale
inline float toDecibels( float gainLinear )
{
	if( gainLinear < kGainNegative100Decibels )
		return 0.0f;
	else
		return 20.0f * log10f( gainLinear * kGainNegative100DecibelsInverse );
}

//! Scale \a array of length \a length from linear (0-1) to decibel (0-100) scale
inline void toDecibels( float *array, size_t length )
{
	for( size_t i = 0; i < length; i++ )
		array[i] = toDecibels( array[i] );
}

//! Scale \a gainLinear from decibel (0-100) to linear (0-1) scale
inline float toLinear( float gainDecibels )
{
	if( gainDecibels < kGainNegative100Decibels )
		return 0.0f;
	else
		return( kGainNegative100Decibels * powf( 10.0f, gainDecibels * 0.05f ) );
}

//! Scale \a array of length \a length from decibel (0-100) to linear (0-1) scale
inline void toLinear( float *array, size_t length )
{
	for( size_t i = 0; i < length; i++ )
		array[i] = toLinear( array[i] );
}

//! Scale \a freq from hertz to MIDI note values, so as one can refer to pitches using the equal temperament scale.
//! For example, 'middle C' equals 261.6 hertz and has a midi value of 60. Adapted from Pure Data's ftom function.
inline float toMidi( float freq )
{
	if( freq < 0 )
		return -1500;

	return 17.3123405046f * math<float>::log( .12231220585f * freq );
}

//! Scale \a midi from MIDI note values to frequency in hertz. Adapted from Pure Data's mtof function. \see toMidi()
inline float toFreq( float midi )
{
	float m = math<float>::clamp( midi, -1499, 1499 );
	return 8.17579891564f * math<float>::exp( .0577622650f * m );
}

// TODO: move to CinderMath.h
inline bool isPowerOf2( size_t val )
{
	return ( val & ( val - 1 ) ) == 0;
}

} } // namespace cinder::audio2