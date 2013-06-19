#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/Utilities.h"

#include "audio2/audio.h"
#include "audio2/GeneratorNode.h"
#include "audio2/Debug.h"

#include "Gui.h"

#include <Accelerate/Accelerate.h>

//#define SOUND_FILE "tone440.wav"
//#define SOUND_FILE "tone440L220R.wav"
#define SOUND_FILE "Blank__Kytt_-_08_-_RSPN.mp3"


// TODO NEXT: only window mFormat.getNumFramesPerBlock() samples of copied buffer - the rest should be zero padded
// should also probably be testing with no zero padding to start (512 frames)

// TODO: mFormat.getFramesPerBlock should set the default fft size

using namespace ci;
using namespace ci::app;
using namespace std;

using namespace audio2;

// impl references:
// - http://stackoverflow.com/a/3534926/506584
// - http://gerrybeauregard.wordpress.com/2013/01/28/using-apples-vdspaccelerate-fft/
// - WebAudio's impl is in core/platform/audio/FFTFrame.h/cpp and audio/mac/FFTFrameMac.cpp

typedef std::shared_ptr<class SpectrumTapNode> SpectrumTapNodeRef;

const float GAIN_THRESH = 0.00001f;			//! threshold for decibel conversion (-100db)
//const float GAIN_THRESH = 0.00004f;
const float GAIN_THRESH_INV = 1 / GAIN_THRESH;

// TODO: double check how to set the minimum of the linear->db conversion
// - maybe provide an optional second param of the lowest value db

//! convert linear (0-1) gain to decibel (0-100) scale
inline float toDecibels( float gainLinear )
{
    if( gainLinear < GAIN_THRESH )
        return 0.0f;
    else
        return 20.0f * log10f( gainLinear * GAIN_THRESH_INV );
}

inline float toLinear( float gainDecibels )
{
    if( gainDecibels < GAIN_THRESH )
        return 0.0f;
    else
        return( GAIN_THRESH * powf( 10.0f, gainDecibels * 0.05f ) );
}

class SpectrumTapNode : public Node {
public:
	// TODO: there should be multiple params, such as window size, fft size (so there can be padding)
	SpectrumTapNode( size_t fftSize = 512 )
	{
		mBufferIsDirty = false;
		mApplyWindow = false;
		mFftSize = forcePow2( fftSize );
		mLog2FftSize = log2f( mFftSize );
		LOG_V << "fftSize: " << mFftSize << ", log2n: " << mLog2FftSize << endl;

	}
	virtual ~SpectrumTapNode() {
		vDSP_destroy_fftsetup( mFftSetup );
	}

	virtual void initialize() override {
		mFftSetup = vDSP_create_fftsetup( mLog2FftSize, FFT_RADIX2 );
		CI_ASSERT( mFftSetup );

		mReal.resize( mFftSize );
		mImag.resize( mFftSize );
		mSplitComplexFrame.realp = mReal.data();
		mSplitComplexFrame.imagp = mImag.data();

		mBuffer = audio2::Buffer( 1, mFftSize );
		mMagSpectrum.resize( mFftSize / 2 );
		LOG_V << "complete" << endl;
	}

	// if mBuffer size is smaller than buffer, only copy enough for mBuffer
	// if buffer size is smaller than mBuffer, just leave the rest as a pad
	// - so, copy the smaller of the two
	// TODO: specify pad, accumulate the required number of samples
	virtual void process( audio2::Buffer *buffer ) override {
		lock_guard<mutex> lock( mMutex );
		copyToInternalBuffer( buffer );
		mBufferIsDirty = true;
	}

	const vector<float>& getMagSpectrum() {
		if( mBufferIsDirty ) {
			lock_guard<mutex> lock( mMutex );

			if( mApplyWindow )
				applyWindow();

			vDSP_ctoz( ( DSPComplex *)mBuffer.getData(), 2, &mSplitComplexFrame, 1, mFftSize / 2 );
			vDSP_fft_zrip( mFftSetup, &mSplitComplexFrame, 1, mLog2FftSize, FFT_FORWARD );


			// Blow away the packed nyquist component.
			mImag[0] = 0.0f;

			// compute normalized magnitude spectrum
			// TODO: try using vDSP_zvabs for this, see if it's any faster (scaling would have to be a different step, but then so is convert to db)
			const float kMagScale = 1.0 / mFftSize;
			for( size_t i = 0; i < mMagSpectrum.size(); i++ ) {
				complex<float> c( mReal[i], mImag[i] );
				mMagSpectrum[i] = abs( c ) * kMagScale;
			}
			mBufferIsDirty = false;
		}
		return mMagSpectrum;
	}

	void setWindowingEnabled( bool b = true )	{ mApplyWindow = b; }
	bool isWindowingEnabled() const				{ return mApplyWindow; }

private:

	size_t forcePow2( size_t val ) {
		if( val & ( val - 1 ) ) {
			LOG_V << "Warning: " << val << " is not a power of 2, rounding up." << endl;
			size_t p = 1;
			while( p < val )
				p *= 2;
			return p;
		}
		return val;
	}

	// TODO: should really be using a Converter to go stereo (or more) -> mono
	// - a good implementation will use equal-power scaling as if the mono signal was two stereo channels panned to center
	void copyToInternalBuffer( audio2::Buffer *buffer ) {
		mBuffer.zero();

		size_t numCopyFrames = std::min( buffer->getNumFrames(), mBuffer.getNumFrames() );
		size_t numSourceChannels = buffer->getNumChannels();
		if( numSourceChannels == 1 ) {
			memcpy( mBuffer.getData(), buffer->getData(), numCopyFrames * sizeof( float ) );
		}
		else {
			// naive average of all channels
			for( size_t ch = 0; ch < numSourceChannels; ch++ ) {
				for( size_t i = 0; i < numCopyFrames; i++ )
					mBuffer[i] += buffer->getChannel( ch )[i];
			}

			float scale = 1.0f / numSourceChannels;
			vDSP_vsmul( mBuffer.getData(), 1 , &scale, mBuffer.getData(), 1, numCopyFrames );
		}

	}

	// TODO: replace this with table lookup
	void applyWindow() {

		// Blackman window
		double alpha = 0.16;
		double a0 = 0.5 * (1 - alpha);
		double a1 = 0.5;
		double a2 = 0.5 * alpha;
		size_t windowSize = std::min( mFftSize, mFormat.getNumFramesPerBlock() );
		double oneOverN = 1.0 / static_cast<double>( windowSize );

		for( size_t i = 0; i < windowSize; ++i ) {
			double x = static_cast<double>(i) * oneOverN;
			double window = a0 - a1 * cos( 2.0 * M_PI * x ) + a2 * cos( 4.0 * M_PI * x );
			mBuffer[i] *= float(window);
		}
	}


	mutex mMutex;

	audio2::Buffer mBuffer;
	std::vector<float> mMagSpectrum;

	atomic<bool> mBufferIsDirty, mApplyWindow;
	size_t mFftSize, mLog2FftSize;
	std::vector<float> mReal, mImag;

	FFTSetup mFftSetup;
	DSPSplitComplex mSplitComplexFrame;
};


class SpectrumTapTestApp : public AppNative {
  public:
	void prepareSettings( Settings *settings );
	void setup();
	void update();
	void draw();

	void initContext();
	void setupUI();
	void processDrag( Vec2i pos );
	void processTap( Vec2i pos );
	void seek( size_t xPos );

	ContextRef mContext;
	PlayerNodeRef mPlayerNode;
	SourceFileRef mSourceFile;

	SpectrumTapNodeRef mSpectrumTap;

	vector<TestWidget *> mWidgets;
	Button mEnableGraphButton, mPlaybackButton, mLoopButton, mApplyWindowButton, mScaleDecibelsButton;
	bool mScaleDecibels;
};


void SpectrumTapTestApp::prepareSettings( Settings *settings )
{
    settings->setWindowSize( 1200, 500 );
}

void SpectrumTapTestApp::setup()
{
	mScaleDecibels = true;
	
	// TODO: convert to unit tests
	LOG_V << "toDecibels( 0 ) = " << toDecibels( 0.0f ) << endl;
	LOG_V << "toDecibels( 0.5 ) = " << toDecibels( 0.5f ) << endl;
	LOG_V << "toDecibels( 1.0 ) = " << toDecibels( 1.0f ) << endl;

	LOG_V << "toLinear( 0 ) = " << toLinear( 0.0f ) << endl;
	LOG_V << "toLinear( 90.0f ) = " << toLinear( 90.0f ) << endl;
	LOG_V << "toLinear( 100.0f ) = " << toLinear( 100.0f ) << endl;

	mContext = Context::instance()->createContext();

	DataSourceRef dataSource = loadResource( SOUND_FILE );
	mSourceFile = SourceFile::create( dataSource, 0, 44100 );
	LOG_V << "output samplerate: " << mSourceFile->getSampleRate() << endl;

	auto audioBuffer = mSourceFile->loadBuffer();

	LOG_V << "loaded source buffer, frames: " << audioBuffer->getNumFrames() << endl;


	mPlayerNode = make_shared<BufferPlayerNode>( audioBuffer );

	mSpectrumTap = make_shared<SpectrumTapNode>( 1024 );

	mPlayerNode->connect( mSpectrumTap )->connect( mContext->getRoot() );

	initContext();
	setupUI();

	mSpectrumTap->start();
	mContext->start();
	mEnableGraphButton.setEnabled( true );

	mApplyWindowButton.setEnabled( mSpectrumTap->isWindowingEnabled() );
	mScaleDecibelsButton.setEnabled( mScaleDecibels );
}

void SpectrumTapTestApp::initContext()
{
	LOG_V << "-------------------------" << endl;
	console() << "Graph configuration: (before)" << endl;
	printGraph( mContext );

	mContext->initialize();

	LOG_V << "-------------------------" << endl;
	console() << "Graph configuration: (after)" << endl;
	printGraph( mContext );
}

void SpectrumTapTestApp::setupUI()
{
	Rectf buttonRect( 0.0f, 0.0f, 200.0f, 60.0f );
	float padding = 10.0f;
	mEnableGraphButton.isToggle = true;
	mEnableGraphButton.titleNormal = "graph off";
	mEnableGraphButton.titleEnabled = "graph on";
	mEnableGraphButton.bounds = buttonRect;
	mWidgets.push_back( &mEnableGraphButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mPlaybackButton.isToggle = true;
	mPlaybackButton.titleNormal = "play sample";
	mPlaybackButton.titleEnabled = "stop sample";
	mPlaybackButton.bounds = buttonRect;
	mWidgets.push_back( &mPlaybackButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mLoopButton.isToggle = true;
	mLoopButton.titleNormal = "loop off";
	mLoopButton.titleEnabled = "loop on";
	mLoopButton.bounds = buttonRect;
	mWidgets.push_back( &mLoopButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mApplyWindowButton.isToggle = true;
	mApplyWindowButton.titleNormal = "apply window";
	mApplyWindowButton.titleEnabled = "apply window";
	mApplyWindowButton.bounds = buttonRect;
	mWidgets.push_back( &mApplyWindowButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mScaleDecibelsButton.isToggle = true;
	mScaleDecibelsButton.titleNormal = "linear";
	mScaleDecibelsButton.titleEnabled = "decibels";
	mScaleDecibelsButton.bounds = buttonRect;
	mWidgets.push_back( &mScaleDecibelsButton );

	getWindow()->getSignalMouseDown().connect( [this] ( MouseEvent &event ) { processTap( event.getPos() ); } );
	getWindow()->getSignalMouseDrag().connect( [this] ( MouseEvent &event ) { processDrag( event.getPos() ); } );
	getWindow()->getSignalTouchesBegan().connect( [this] ( TouchEvent &event ) { processTap( event.getTouches().front().getPos() ); } );
	getWindow()->getSignalTouchesMoved().connect( [this] ( TouchEvent &event ) {
		for( const TouchEvent::Touch &touch : getActiveTouches() )
			processDrag( touch.getPos() );
	} );

	gl::enableAlphaBlending();
}

void SpectrumTapTestApp::seek( size_t xPos )
{
	size_t seek = mPlayerNode->getNumFrames() * xPos / getWindowWidth();
	mPlayerNode->setReadPosition( seek );
}

void SpectrumTapTestApp::processDrag( Vec2i pos )
{
	seek( pos.x );
}

// TODO: currently makes sense to enable processor + tap together - consider making these enabled together.
// - possible solution: add a silent flag that is settable by client
void SpectrumTapTestApp::processTap( Vec2i pos )
{
	if( mEnableGraphButton.hitTest( pos ) )
		mContext->setEnabled( ! mContext->isEnabled() );
	else if( mPlaybackButton.hitTest( pos ) )
		mPlayerNode->setEnabled( ! mPlayerNode->isEnabled() );
	else if( mLoopButton.hitTest( pos ) )
		mPlayerNode->setLoop( ! mPlayerNode->getLoop() );
	else if( mApplyWindowButton.hitTest( pos ) )
		mSpectrumTap->setWindowingEnabled( ! mSpectrumTap->isWindowingEnabled() );
	else if( mScaleDecibelsButton.hitTest( pos ) )
		mScaleDecibels = ! mScaleDecibels;
	else
		seek( pos.x );
}

void SpectrumTapTestApp::update()
{
	// update playback button, since the player node may stop itself at the end of a file.
	if( ! mPlayerNode->isEnabled() )
		mPlaybackButton.setEnabled( false );
}

void SpectrumTapTestApp::draw()
{
	gl::clear();

	// draw magnitude spectrum bins

	auto &mag = mSpectrumTap->getMagSpectrum();
	size_t numBins = mag.size();
	float margin = 40.0f;
	float padding = 0.0f;
	float binWidth = ( (float)getWindowWidth() - margin * 2.0f - padding * ( numBins - 1 ) ) / (float)numBins;
	float binYScaler = ( (float)getWindowHeight() - margin * 2.0f );

	Rectf bin( margin, getWindowHeight() - margin, margin + binWidth, getWindowHeight() - margin );
	for( size_t i = 0; i < numBins; i++ ) {
		float h = mag[i];
		if( mScaleDecibels ) {
			h = toDecibels( h ) / 100.0f;
//			if( h < 0.3f )
//				h = 0.0f;
		}
		bin.y1 = bin.y2 - h * binYScaler;
		gl::color( 0.0f, 0.9f, 0.0f );
		gl::drawSolidRect( bin );

		bin += Vec2f( binWidth + padding, 0.0f );
	}

	auto min = min_element( mag.begin(), mag.end() );
	auto max = max_element( mag.begin(), mag.end() );

	string info = string( "min: " ) + toString( *min ) + string( ", max: " ) + toString( *max );
	gl::drawString( info, Vec2f( margin, getWindowHeight() - 30.0f ) );

	drawWidgets( mWidgets );
}

CINDER_APP_NATIVE( SpectrumTapTestApp, RendererGl )
