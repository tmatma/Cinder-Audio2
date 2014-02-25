/*
Copyright (c) 2014, The Cinder Project

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

#include "cinder/audio2/dsp/WaveTable.h"
#include "cinder/audio2/dsp/Dsp.h"
#include "cinder/audio2/Utilities.h"
#include "cinder/audio2/Debug.h"
#include "cinder/CinderMath.h"

#include "cinder/Timer.h" // TEMP

#define DEFAULT_WAVETABLE_SIZE 4096
#define DEFAULT_NUM_WAVETABLES 40

using namespace std;

namespace {

// gibbs effect reduction based on http://www.musicdsp.org/files/bandlimited.pdf
inline float calcGibbsReduceCoeff( size_t partial, size_t numPartials )
{
	if( numPartials <= 1 )
		return 1;

	float result = ci::math<float>::cos( (float)partial * M_PI * 0.5f / numPartials );
	return result * result;
}

inline float calcTableIndex( float f0Midi, float minRange, float maxRange, size_t numTables )
{
	const float midiRangePerTable = ( maxRange - minRange ) / ( numTables - 1 );
	return 1 + ( f0Midi - minRange ) / midiRangePerTable;
}

} // anonymous namespace

namespace cinder { namespace audio2 { namespace dsp {

WaveTable::WaveTable( size_t sampleRate, size_t tableSize )
	: mSampleRate( sampleRate ), mTableSize( tableSize ? tableSize : DEFAULT_WAVETABLE_SIZE )
{
}

void WaveTable::resize( size_t tableSize )
{
	if( mTableSize == tableSize && mBuffer.getNumFrames() == tableSize )
		return;

	mTableSize = tableSize;
	mBuffer.setSize( tableSize, 0 );
}

WaveTable2d::WaveTable2d( size_t sampleRate, size_t tableSize, size_t numTables )
	: WaveTable( sampleRate, tableSize ), mNumTables( numTables ? numTables : DEFAULT_NUM_WAVETABLES )
{
	calcLimits();
}

void WaveTable2d::setSampleRate( size_t sampleRate )
{
	mSampleRate = sampleRate;
	calcLimits();
}

void WaveTable2d::resize( size_t tableSize, size_t numTables )
{
	bool needsResize = false;
	if( mTableSize != tableSize || mBuffer.getNumFrames() != tableSize ) {
		mTableSize = tableSize;
		needsResize = true;
	}
	if( mNumTables != numTables || mBuffer.getNumChannels() != numTables ) {
		mNumTables = numTables;
		needsResize = true;
	}

	if( needsResize )
		mBuffer.setSize( mTableSize, mNumTables );
}

void WaveTable2d::fillBandlimited( WaveformType type )
{
	LOG_V( "filling " << mNumTables << " tables of size: " << mTableSize << "..." );
	Timer timer( true );

	resize( mTableSize, mNumTables );

	for( size_t i = 0; i < mNumTables; i++ ) {
		float *table = mBuffer.getChannel( i );

		// last table always has only one partial
		if( i == mNumTables - 1 ) {
			fillBandLimitedTable( type, table, 1 );
			LOG_V( "\t[" << i << "] LAST, nyquist / 4 and above, max partials: 1 " );
			break;
		}

		size_t maxPartialsForFreq = getMaxHarmonicsForTable( i );
		fillBandLimitedTable( type, table, maxPartialsForFreq );
	}

	LOG_V( "..done, seconds: " << timer.getSeconds() );
}

// note: for at least sawtooth and square, this must be recomputed for every table so that gibbs reduction is accurate
void WaveTable2d::fillBandLimitedTable( WaveformType type, float *table, size_t numPartials )
{
	vector<float> partials;
	if( type == WaveformType::SINE )
		partials.resize( 1 );
	else
		partials.resize( numPartials );

	switch( type ) {
		case WaveformType::SINE:
			partials[0] = 1;
			break;
		case WaveformType::SQUARE:
			// 1 / x for odd x
			for( size_t x = 1; x <= partials.size(); x += 2 ) {
				float m = calcGibbsReduceCoeff( x, partials.size() );
				partials[x - 1] = m / float( x );
			}
			break;
		case WaveformType::SAWTOOTH:
			// 1 / x
			for( size_t x = 1; x <= numPartials; x += 1 ) {
				float m = calcGibbsReduceCoeff( x, partials.size() );
				partials[x - 1] = m / float( x );
			}
			break;
		case WaveformType::TRIANGLE: {
			// 1 / x^2 for odd x, alternating + and -
			float t = 1;
			for( size_t x = 1; x <= partials.size(); x += 2 ) {
				partials[x - 1] = t / float( x * x );
				t *= -1;
			}
			break;
		}
		default:
			CI_ASSERT_NOT_REACHABLE();
	}

	fillSinesum( table, mTableSize, partials );
	dsp::normalize( table, mTableSize );
}

void WaveTable2d::fillSinesum( float *array, size_t length, const std::vector<float> &partials )
{
	memset( array, 0, length * sizeof( float ) );

	double phase = 0;
	const double phaseIncr = ( 2.0 * M_PI ) / (double)length;

	for( size_t i = 0; i < length; i++ ) {
		double partialPhase = phase;
		for( size_t p = 0; p < partials.size(); p++ ) {
			array[i] += partials[p] * math<float>::sin( partialPhase );
			partialPhase += phase;
		}
		
		phase += phaseIncr;
	}
}

size_t WaveTable2d::getMaxHarmonicsForTable( size_t tableIndex ) const
{
	const float nyquist = (float)mSampleRate / 2.0f;
	const float midiRangePerTable = ( mMaxMidiRange - mMinMidiRange ) / ( mNumTables - 1 );
	const float maxMidi = mMinMidiRange + tableIndex * midiRangePerTable;
	const float maxF0 = toFreq( maxMidi );

	size_t maxPartialsForFreq( nyquist / maxF0 );

	LOG_V( "\t[" << tableIndex << "] midi: " << maxMidi << ", max f0: " << maxF0 << ", max partials: " << maxPartialsForFreq );
	return maxPartialsForFreq;
}

float WaveTable2d::calcBandlimitedTableIndex( float f0 ) const
{
	CI_ASSERT_MSG( f0 >= 0, "negative frequencies not yet handled" ); // TODO: negate in GenOscillator

	const float f0Midi = toMidi( f0 );

	if( f0Midi <= mMinMidiRange )
		return 0;
	else if( f0Midi >= mMaxMidiRange )
		return mNumTables - 1;

	return calcTableIndex( f0Midi, mMinMidiRange, mMaxMidiRange, mNumTables );
}

const float* WaveTable2d::getBandLimitedTable( float f0 ) const
{
	size_t index = (size_t)calcBandlimitedTableIndex( f0 );
	return mBuffer.getChannel( index );
}

std::tuple<const float*, const float*, float> WaveTable2d::getBandLimitedTablesLerp( float f0 ) const
{
	CI_ASSERT_MSG( f0 >= 0, "negative frequencies not yet handled" ); // TODO: negate in GenOscillator

	float *table1, *table2;
	float factor;

	const float f0Midi = toMidi( f0 );

	if( f0Midi <= mMinMidiRange ) {
		table1 = table2 = const_cast<float *>( mBuffer.getChannel( 0 ) );
		factor = 0;
	}
	else if( f0Midi >= mMaxMidiRange ) {
		table1 = table2 = const_cast<float *>( mBuffer.getChannel( mNumTables - 1 ) );
		factor = 1;
	}
	else {

		float index = calcTableIndex( f0Midi, mMinMidiRange, mMaxMidiRange, mNumTables );

		size_t tableIndex1 = (size_t)index;
		size_t tableIndex2 = ( tableIndex1 + 1 ) & ( mTableSize - 1 );

		table1 = const_cast<float *>( mBuffer.getChannel( tableIndex1 ) );
		table2 = const_cast<float *>( mBuffer.getChannel( tableIndex2 ) );
		factor = index - (float)tableIndex1;
	}

	return make_tuple( table1, table2, factor );
}

namespace {

#if 0

// truncate, phase range: 0-1
inline float tableLookup( const float *table, size_t size, float phase )
{
	return table[ size_t( phase * size ) ];
}

#else

// linear interpolation, phase range: 0-1
// TODO (optimization): store phase in range 0-size
inline float tableLookup( const float *table, size_t size, float phase )
{
	float lookup = phase * size;
	size_t index1 = (size_t)lookup;
	size_t index2 = ( index1 + 1 ) & ( size - 1 );
	float val1 = table[index1];
	float val2 = table[index2];
	float frac = lookup - (float)index1;

	return val2 + frac * ( val2 - val1 );
}

#endif
		
} // anonymous namespace

float WaveTable2d::lookupBandlimited( float phase, float f0 ) const
{
	const float *table = getBandLimitedTable( f0 );
	return tableLookup( table, mTableSize, phase );
}

#if 1

// no table interpolation

float WaveTable2d::lookupBandlimited( float *outputArray, size_t outputLength, float currentPhase, float f0 ) const
{
	const float phaseIncr = f0 / (float)mSampleRate;
	const float *table = getBandLimitedTable( f0 );
	const size_t tableSize = mTableSize;

	for( size_t i = 0; i < outputLength; i++ ) {
		outputArray[i] = tableLookup( table, tableSize, currentPhase );
		currentPhase = fmodf( currentPhase + phaseIncr, 1 );
	}

	return currentPhase;
}

float WaveTable2d::lookupBandlimited( float *outputArray, size_t outputLength, float currentPhase, const float *f0Array ) const
{
	const size_t tableSize = mTableSize;
	const float samplePeriod = 1.0f / mSampleRate;

	for( size_t i = 0; i < outputLength; i++ ) {
		const float f0 = f0Array[i];
		const float *table = getBandLimitedTable( f0 );

		outputArray[i] = tableLookup( table, tableSize, currentPhase );
		currentPhase = fmodf( currentPhase + f0 * samplePeriod, 1 );
	}

	return currentPhase;
}

#else

// table interpoloation

float WaveTable2d::lookup( float *outputArray, size_t outputLength, float currentPhase, float f0 ) const
{
	const float phaseIncr = f0 / (float)mSampleRate;
	const size_t tableSize = mTableSize;
	auto tables = getBandLimitedTablesLerp( f0 );


	for( size_t i = 0; i < outputLength; i++ ) {
		float a = tableLookup( get<0>( tables ), tableSize, currentPhase );
		float b = tableLookup( get<1>( tables ), tableSize, currentPhase );
		outputArray[i] = lerp( a, b, get<2>( tables ) );
		currentPhase = fmodf( currentPhase + phaseIncr, 1 );
	}

	return currentPhase;
}

float WaveTable2d::lookup( float *outputArray, size_t outputLength, float currentPhase, const float *f0Array ) const
{
	const size_t tableSize = mTableSize;
	const float samplePeriod = 1.0f / mSampleRate;

	for( size_t i = 0; i < outputLength; i++ ) {
		const float f0 = f0Array[i];
		auto tables = getBandLimitedTablesLerp( f0 );

		float a = tableLookup( get<0>( tables ), tableSize, currentPhase );
		float b = tableLookup( get<1>( tables ), tableSize, currentPhase );
		outputArray[i] = lerp( a, b, get<2>( tables ) );
		currentPhase = fmodf( currentPhase + f0 * samplePeriod, 1 );
	}

	return currentPhase;
}

#endif

void WaveTable2d::copyTo( float *array, size_t tableIndex ) const
{
	CI_ASSERT( tableIndex < mNumTables );

	memcpy( array, mBuffer.getChannel( tableIndex ), mTableSize * sizeof( float ) );
}

void WaveTable2d::calcLimits()
{
	mMinMidiRange = toMidi( 20 );
	mMaxMidiRange = toMidi( (float)mSampleRate / 4.0f ); // everything above can only have one partial
}

} } } // namespace cinder::audio2::dsp