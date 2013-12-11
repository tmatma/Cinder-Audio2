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
#include "cinder/audio2/Buffer.h"
#include "cinder/audio2/NodeEffect.h"
#include "cinder/audio2/msw/xaudio.h"
#include "cinder/audio2/msw/util.h"

namespace cinder { namespace audio2 { namespace msw {

class NodeXAudioSourceVoice;
class NodeXAudio;

class NodeXAudio {
  public:
	NodeXAudio() {}
	virtual ~NodeXAudio();

  protected:
	std::vector<::XAUDIO2_EFFECT_DESCRIPTOR>	mEffectsDescriptors;
};

struct VoiceCallbackImpl;

class NodeXAudioSourceVoice : public Node, public NodeXAudio {
public:
	virtual	~NodeXAudioSourceVoice();

	std::string virtual getName()				{ return "SourceVoiceXAudio"; }

	void initialize() override;
	void uninitialize() override;

	void start() override;
	void stop() override;

	//! Returns the native \a IXAudio2SourceVoice maintained by the \a Node.
	IXAudio2SourceVoice* getNative()	{ return mSourceVoice; }

	std::vector<::XAUDIO2_EFFECT_DESCRIPTOR>& getEffectsDescriptors() { return mEffectsDescriptors; }

private:
	NodeXAudioSourceVoice();

	void initSourceVoice();
	void uninitSourceVoice();

	void submitNextBuffer();

	::IXAudio2SourceVoice						*mSourceVoice;
	::XAUDIO2_BUFFER							mXAudioBuffer;
	std::vector<::XAUDIO2_EFFECT_DESCRIPTOR>	mEffectsDescriptors;
	BufferInterleaved							mBufferInterleaved;
	std::unique_ptr<VoiceCallbackImpl>			mVoiceCallback;

	friend class ContextXAudio;
	friend class NodeEffectXAudioXapo;
};

struct EngineCallbackImpl;

class LineOutXAudio : public LineOut, public NodeXAudio {
  public:
	LineOutXAudio( DeviceRef device, const Format &format = Format() );
	virtual ~LineOutXAudio();

	std::string virtual getName()				{ return "LineOutXAudio"; }

	void initialize() override;
	void uninitialize() override;

	void start() override;
	void stop() override;

	uint64_t getNumProcessedFrames() override	{ return mProcessedFrames; }
	uint64_t getLastClip() override;

	bool supportsInputNumChannels( size_t numChannels ) override;

	::IXAudio2* getXAudio() const	{ return mXAudio; }

  private:
	::IXAudio2					*mXAudio;
	::IXAudio2MasteringVoice	*mMasteringVoice;
	std::atomic<uint64_t>		mProcessedFrames;

	std::unique_ptr<EngineCallbackImpl> mEngineCallback;

	friend EngineCallbackImpl;
};

class NodeEffectXAudioXapo : public NodeEffect, public NodeXAudio {
public:
	//! These enum names match the class uuid names in xapofx.h. TODO: consider just passing in the REFCLSID
	enum XapoType { FXEcho, FXEQ, FXMasteringLimiter, FXReverb };

	NodeEffectXAudioXapo( XapoType type, const Format &format = Format() );
	virtual ~NodeEffectXAudioXapo();

	std::string virtual getName()				{ return "EffectXAudioXapo"; }

	void initialize() override;

	// TODO: get/set params should throw if a bad HRESULT shows up because of this

	template<typename ParamsT>
	void getParams( ParamsT *params )			{ getParams( static_cast<void *>( params ), sizeof( *params ) ); }

	template<typename ParamsT>
	void setParams( const ParamsT &params )		{ setParams( static_cast<const void *>( &params ), sizeof( params ) ); }
 
private:
	void getParams( void *params, size_t sizeParams );
	void setParams( const void *params, size_t sizeParams );

	void makeXapo( REFCLSID clsid );
	void notifyConnected();

	std::unique_ptr<::IUnknown, msw::ComReleaser>	mXapo;
	XapoType										mType;
	::XAUDIO2_EFFECT_DESCRIPTOR						mEffectDesc;
	size_t											mChainIndex;

	friend class ContextXAudio;
};

//! \note Due to XAudio2 limitations, only one EffectXAudioFilter can be attached in series. Connecting another will simply overwrite the first and you will only hear the second filter.
class NodeEffectXAudioFilter : public NodeEffect, public NodeXAudio {
public:

	NodeEffectXAudioFilter( const Format &format = Format() );
	virtual ~NodeEffectXAudioFilter();

	std::string virtual getName()				{ return "EffectXAudioFilter"; }

	void initialize() override;
	void uninitialize() override;

	void getParams( ::XAUDIO2_FILTER_PARAMETERS *params );
	void setParams( const ::XAUDIO2_FILTER_PARAMETERS &params );
};

class ContextXAudio : public Context {
  public:
	ContextXAudio();
	virtual ~ContextXAudio();

	LineOutRef	createLineOut( const DeviceRef &device, const Node::Format &format = Node::Format() ) override;
	//! If deployment target is 0x601 (win vista) or greater, uses \a LineInWasapi, else returns an empty \a LineInRef
	LineInRef	createLineIn( const DeviceRef &device, const Node::Format &format = Node::Format()  ) override;

	//! When connections change, ensure a NodeXAudioSourceVoice is in the right position to enable pulling audio samples.
	void connectionsDidChange( const NodeRef &node ) override; 
	//! Overridden to assert type is LineOutXAudio
	void setTarget( const NodeTargetRef &target ) override;
	//! Returns a pointer to the \a IXAudio2 instance associated with this context, owned by the associated \a LineOut.
	::IXAudio2* getXAudio() const	{ return std::dynamic_pointer_cast<LineOutXAudio>( mTarget )->getXAudio(); }

	//! Sets whether to enable filter usage within this audio context (default = true). \see NodeEffectXAudioFilter
	void setFilterEffectsEnabled( bool b = true )	{ mFilterEnabled = b; }
	//! Returns whether filter usage is enabled within this audio context (default = true). \see NodeEffectXAudioFilter
	bool isFilterEffectsEnabled() const			{ return mFilterEnabled; }

  private:
	bool	mFilterEnabled;
};

} } } // namespace cinder::audio2::msw