#include "audio2/Plot.h"

#include "cinder/CinderMath.h"
#include "cinder/Triangulate.h"
#include "cinder/gl/gl.h"

using namespace std;
using namespace ci;

namespace audio2 {

inline void calcMinMaxForSection( const float *buffer, int samplesPerSection, float &max, float &min ) {
	max = 0.0f;
	min = 0.0f;
	for( int k = 0; k < samplesPerSection; k++ ) {
		float s = buffer[k];
		max = math<float>::max( max, s );
		min = math<float>::min( min, s );
	}
}

inline void calcAverageForSection( const float *buffer, int samplesPerSection, float &upper, float &lower ) {
	upper = 0.0f;
	lower = 0.0f;
	for( int k = 0; k < samplesPerSection; k++ ) {
		float s = buffer[k];
		if( s > 0.0f ) {
			upper += s;
		} else {
			lower += s;
		}
	}
	upper /= samplesPerSection;
	lower /= samplesPerSection;
}

Waveform::Waveform( const vector<float> &samples, const Vec2i &screenDimensions, int pixelsPerVertex, CalcMode mode )
{
    float height = screenDimensions.y / 2.0f;
    int numSections = screenDimensions.x / pixelsPerVertex + 1;
    int samplesPerSection = samples.size() / numSections;

	vector<Vec2f> &points = mOutline.getPoints();
	points.resize( numSections * 2 );

    for( int i = 0; i < numSections; i++ ) {
		float x = i * pixelsPerVertex;
		float yUpper, yLower;
		if( mode == CalcMode::MinMax ) {
			calcMinMaxForSection( &samples[i * samplesPerSection], samplesPerSection, yUpper, yLower );
		} else {
			calcAverageForSection( &samples[i * samplesPerSection], samplesPerSection, yUpper, yLower );
		}
		points[i] = Vec2f( x, height - height * yUpper );
		points[numSections * 2 - i - 1] = Vec2f( x, height - height * yLower );
    }
	mOutline.setClosed();
}

const TriMesh2d& Waveform::getMesh()
{
	if( mMesh.getVertices().size() == 0 ) {
		Triangulator tess( mOutline );
		mMesh = tess.calcMesh();
	}
	return mMesh;
}

void WaveformPlot::load( const std::vector<float> &channel, const ci::Rectf &bounds, int pixelsPerVertex )
{
	mWaveforms.clear();

	Vec2i waveSize = bounds.getSize();
	mWaveforms.push_back( Waveform( channel, waveSize, pixelsPerVertex, Waveform::CalcMode::MinMax ) );
	mWaveforms.push_back( Waveform( channel, waveSize, pixelsPerVertex, Waveform::CalcMode::Average ) );
}

void WaveformPlot::load( const std::vector<std::vector<float>> &channels, const Rectf &bounds, int pixelsPerVertex )
{
	mBounds = bounds;
	mWaveforms.clear();

	int numChannels = channels.size();
	Vec2i waveSize = bounds.getSize();
	waveSize.y /= numChannels;
	for( int i = 0; i < numChannels; i++ ) {
		mWaveforms.push_back( Waveform( channels[i], waveSize, pixelsPerVertex, Waveform::CalcMode::MinMax ) );
		mWaveforms.push_back( Waveform( channels[i], waveSize, pixelsPerVertex, Waveform::CalcMode::Average ) );
	}
}

void WaveformPlot::drawGl()
{
    if( mWaveforms.empty() ) {
        return;
    }

	gl::color( mColorMinMax );
	gl::draw( mWaveforms[0].getMesh() );

	gl::color( mColorAvg );
	gl::draw( mWaveforms[1].getMesh() );

	if( mWaveforms.size() > 2 ) {

		gl::pushMatrices();
		gl::translate( 0.0f, mBounds.getHeight() / 2 );

		gl::color( mColorMinMax );
		gl::draw( mWaveforms[2].getMesh() );

		gl::color( mColorAvg );
		gl::draw( mWaveforms[3].getMesh() );

		gl::popMatrices();
	}
}

} // namespace audio2