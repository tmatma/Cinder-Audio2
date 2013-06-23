#pragma once

#include "audio2/Device.h"
#include "audio2/Buffer.h"
#include "audio2/Atomic.h"

#include <memory>
#include <vector>

namespace audio2 {

class GeneratorNode;

typedef std::shared_ptr<class Context> ContextRef;
typedef std::shared_ptr<class Node> NodeRef;

typedef std::shared_ptr<class MixerNode> MixerNodeRef;
typedef std::shared_ptr<class RootNode> RootNodeRef;
typedef std::shared_ptr<class OutputNode> OutputNodeRef;

typedef std::shared_ptr<class GeneratorNode> GeneratorNodeRef;
typedef std::shared_ptr<class InputNode> InputNodeRef;
typedef std::shared_ptr<class FilePlayerNode> FilePlayerNodeRef;

class Node : public std::enable_shared_from_this<Node> {
  public:
	virtual ~Node();

	struct Format {
		Format() : mChannels( 0 ), mWantsDefaultFormatFromParent( false ) {}
		
		Format& channels( size_t ch )							{ mChannels = ch; return *this; }
		Format& wantsDefaultFormatFromParent( bool b = true )	{ mWantsDefaultFormatFromParent = b; return *this; }

		size_t	getChannels() const								{ return mChannels; }
		bool	getWantsDefaultFormatFromParent() const			{ return mWantsDefaultFormatFromParent; }

	  private:
		size_t mChannels;
		bool mWantsDefaultFormatFromParent;
	};

	virtual void initialize()	{ mInitialized = true; }
	virtual void uninitialize()	{ mInitialized = false; }
	virtual void start()		{ mEnabled = true; }
	virtual void stop()			{ mEnabled = false; }

	virtual NodeRef connect( NodeRef dest );
	virtual NodeRef connect( NodeRef dest, size_t bus );

	virtual void disconnect( size_t bus = 0 );

	virtual void setSource( NodeRef source );
	virtual void setSource( NodeRef source, size_t bus );

	size_t	getNumChannels() const	{ return mNumChannels; }
//	void	setNumChannels( size_t numChannels )	{ mNumChannels = numChannels; mNumChannelsUnspecified = false; }

	bool	isNumChannelsUnspecified() const	{ return mNumChannelsUnspecified; }
	bool	getWantsDefaultFormatFromParent() const			{ return mWantsDefaultFormatFromParent; }

//	void	setWantsDefaultFormatFromParent( bool b = true )	{ mWantsDefaultFormatFromParent = b; }

	const Buffer::Layout& getBufferLayout() const { return mBufferLayout; }

	//! controls whether the owning Context automatically enables / disables this Node
	bool	isAutoEnabled() const				{ return mAutoEnabled; }
	void	setAutoEnabled( bool b = true )		{ mAutoEnabled = b; }

	//! Default implementation returns true if numChannels match our format
	virtual bool supportsSourceNumChannels( size_t numChannels ) const	{ return mNumChannels == numChannels; }

	//! If required Format properties are missing, fill in params from parent tree
	virtual void fillFormatParamsFromParent();

	//! If required Format properties are missing, fill in params from first source
	virtual void fillFormatParamsFromSource();

	virtual void process( Buffer *buffer )	{}

	std::vector<NodeRef>& getSources()			{ return mSources; }
	NodeRef getParent()	const					{ return mParent.lock(); }
	void setParent( NodeRef parent )			{ mParent = parent; }

	ContextRef getContext() const				{ return mContext.lock(); }
	void setContext( ContextRef context )		{ mContext = context; }

	const std::string& getTag()	const	{ return mTag; }

	bool isInitialized() const	{ return mInitialized; }
	bool isEnabled() const		{ return mEnabled; }

	void setEnabled( bool b = true );

  protected:
	Node( const Format &format );

	//! If required Format properties are missing, fill in from \a otherFormat
	virtual void fillFormatParamsFromNode( const NodeRef &otherNode );

	std::vector<NodeRef>	mSources;
	std::weak_ptr<Node>		mParent;
	std::weak_ptr<Context>	mContext;
	bool					mInitialized;
	std::atomic<bool>		mEnabled;
	std::string				mTag;

	size_t mNumChannels;
	bool mWantsDefaultFormatFromParent, mNumChannelsUnspecified;
	bool mAutoEnabled;
	Buffer::Layout			mBufferLayout;


  private:
	  Node( Node const& );
	  Node& operator=( Node const& );
};

class RootNode : public Node {
  public:
	RootNode( const Format &format = Format() ) : Node( format ) {}
	virtual ~RootNode() {}

	// TODO: need to decide where user sets the samplerate / blocksize - on RootNode or Context?
	// - this is still needed to determine a default
	// - also RootNode has to agree with the sampleate - be it output out, file out, whatever
	virtual size_t getSampleRate() = 0;
	virtual size_t getNumFramesPerBlock() = 0;

  private:
	// RootNode subclasses cannot connect to anything else
	NodeRef connect( NodeRef dest ) override				{ return NodeRef(); }
	NodeRef connect( NodeRef dest, size_t bus ) override	{ return NodeRef(); }
};

class OutputNode : public RootNode {
  public:

	// ???: device param here necessary?
	OutputNode( DeviceRef device, const Format &format = Format() ) : RootNode( format ) {
		setAutoEnabled();
	}
	virtual ~OutputNode() {}

	virtual DeviceRef getDevice() = 0;

	size_t getSampleRate()			{ return getDevice()->getSampleRate(); }
	size_t getNumFramesPerBlock()	{ return getDevice()->getNumFramesPerBlock(); }

  protected:
};

// TODO: Because busses can be expanded, the naming is off:
//		- mMaxNumBusses should be mNumBusses, there is no max
//			- so there is getNumBusses() / setNumBusses()
//		- there can be 'holes', slots in mSources that are not used
//		- getNumActiveBusses() returns number of used slots

class MixerNode : public Node {
  public:
	MixerNode( const Format &format = Format() ) : Node( format ), mMaxNumBusses( 10 ) { mSources.resize( mMaxNumBusses ); }
	virtual ~MixerNode() {}

	using Node::setSource;
	//! Mixers will append the node if setSource() is called without a bus number.
	virtual void setSource( NodeRef source ) override;

	//! returns the number of connected busses.
	virtual size_t getNumBusses() = 0;
	virtual void setNumBusses( size_t count ) = 0;	// ???: does this make sense now? should above be getNumActiveBusses?
	virtual size_t getMaxNumBusses()	{ return mMaxNumBusses; }
	virtual void setMaxNumBusses( size_t count ) = 0;
	virtual bool isBusEnabled( size_t bus ) = 0;
	virtual void setBusEnabled( size_t bus, bool enabled = true ) = 0;
	virtual void setBusVolume( size_t bus, float volume ) = 0;
	virtual float getBusVolume( size_t bus ) = 0;
	virtual void setBusPan( size_t bus, float pan ) = 0;
	virtual float getBusPan( size_t bus ) = 0;

protected:
	size_t mMaxNumBusses;
};

class Context : public std::enable_shared_from_this<Context> {
  public:
	virtual ~Context();

	virtual ContextRef			createContext() = 0;
	virtual MixerNodeRef		createMixer() = 0;
	virtual OutputNodeRef		createOutput( DeviceRef device = Device::getDefaultOutput() ) = 0;
	virtual InputNodeRef		createInput( DeviceRef device = Device::getDefaultInput() ) = 0;

	static Context* instance();

	virtual void initialize();
	virtual void uninitialize();
	virtual void setRoot( RootNodeRef root )	{ mRoot = root; }

	//! If the root has not already been set, it is the default OutputNode
	virtual RootNodeRef getRoot();
	virtual void start();
	virtual void stop();


	bool isInitialized() const	{ return mInitialized; }

	bool isEnabled() const		{ return mEnabled; }

	//! convenience method to start / stop the graph via bool
	void setEnabled( bool enabled = true );


	size_t getSampleRate() const			{ return mSampleRate; }
	size_t getNumFramesPerBlock() const		{ return mNumFramesPerBlock; }

  protected:
	Context() : mInitialized( false ), mEnabled( false ) {}

	virtual void start( NodeRef node );
	virtual void stop( NodeRef node );


	RootNodeRef		mRoot;
	bool			mInitialized, mEnabled;
	size_t			mSampleRate, mNumFramesPerBlock;
};

} // namespace audio2