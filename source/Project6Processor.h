//------------------------------------------------------------------------
// Project6 - audio processor
//
// Project6 is an INSTRUMENT: an event input and one stereo audio output,
// no audio input. That decision reaches four places and they must agree -
// PlugType::kInstrumentSynth in Project6Entry.cpp, the buses added in
// initialize(), what setBusArrangements accepts, and the 0-in / 2-out
// entry in resource/au-info.plist. auval checks the last two against each
// other.
//
// The DSP renders silence. What is here is the bus layout, the parameter
// plumbing, the event handling and the state - which is exactly what the
// SDK validator exercises, and getting it validating before there is any
// audio to blame is the point of doing it in this order.
//
// DELIBERATELY ABSENT: processContextRequirements. Since VST3 3.7 the
// ProcessContext is opt-in and the default is NO FLAGS, so
// data.processContext arrives empty and anything that reads the tempo
// silently gets 120 in every host. The validator prints "- None" and does
// not complain. If this plug-in ever needs the transport, add
//
//     #include "public.sdk/source/vst/utility/processcontextrequirements.h"
//     ...
//     Steinberg::Vst::IProcessContextRequirements::Flags
//     PLUGIN_API getProcessContextRequirements () SMTG_OVERRIDE
//     { return Steinberg::Vst::IProcessContextRequirements::kNeedTempo; }
//
// and inherit IProcessContextRequirements alongside AudioEffect.
//------------------------------------------------------------------------

#pragma once

#include "Project6Dsp.h"
#include "Project6Params.h"
#include "Project6Sample.h"
#include "Project6Slots.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace Project6 {

//------------------------------------------------------------------------
class Project6Processor : public Steinberg::Vst::AudioEffect
{
public:
	Project6Processor ();

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IAudioProcessor*)new Project6Processor;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	/** Receives a slot's file path from the controller. */
	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);
	void sendSampleRateToController ();

	//--------------------------------------------------------------------
	// Sample loading
	//
	// ALL OF THIS IS UI-THREAD ONLY. It opens files, allocates, decodes
	// and frees - none of which may happen while a block is being
	// rendered.
	//--------------------------------------------------------------------

	/** Read whatever is in a slot's path, publish it, and tell the
	    controller how it went. */
	void loadSlot (int index);

	/** Load every slot that has a path. Called after the state stream has
	    filled the bank. */
	void loadAllSlots ();

	/** Hand a decoded buffer to the DSP and retire the one it replaces.

	    THE RETIREMENT IS THE POINT. The audio thread may be half way
	    through a block holding the old pointer, so the old buffer cannot
	    be freed here; it goes on mRetired and is freed later, once the
	    block counter proves no block that could have seen it is still
	    running. */
	void publishSlot (int index, std::shared_ptr<const SampleBuffer> sample);

	/** Free the retired buffers it is now safe to free. `force` is for
	    teardown, where there is no audio thread left to wait for. */
	void collectRetired (bool force);

	void sendSlotStatusToController (int index);
	void sendAllSlotStatusesToController ();
	/** Note-on / note-off at one sample offset. Nothing sounds yet; the
	    events are consumed anyway so that when something does, the
	    sample-accurate path underneath it is already the one being used. */
	void handleEvent (const Steinberg::Vst::Event& event);
	/** Render `numSamples` into the interleaved scratch, then copy to the
	    bus at `offset`. */
	void renderSegment (Steinberg::Vst::ProcessData& data, Steinberg::int32 offset,
	                    Steinberg::int32 numSamples);

	/** Normalised value of every automated parameter, indexed by ParamID. */
	double mParams[kNumParams] = {};
	bool   mBypass = false;
	double mSampleRate = 44100.0;

	/** THE AUTHORITATIVE COPY of the sample slots: this is the half of
	    the plug-in the host asks for the project's state, so this is the
	    bank that gets saved. The controller keeps its own for the panel
	    and the two are kept in step by the message and the state stream,
	    never by reaching across.

	    Written on the UI thread, from notify(). NOTHING ON THE AUDIO
	    THREAD READS IT YET - and when the sample loading arrives, what
	    process() reads must be a prepared buffer handed over by a
	    non-audio thread, never these strings: a std::string assignment
	    allocates, and process() must not. */
	SlotBank mSlots;

	/** The decoded audio, OWNED HERE. The DSP holds bare pointers into
	    these; nothing else may. */
	std::shared_ptr<const SampleBuffer> mSamples[kSlotCount];

	/** How each slot's file read, so the panel can say why one will not
	    play. */
	SampleStatus mStatus[kSlotCount] = {};

	/** A buffer that has been replaced, and the block number at which it
	    was replaced. */
	struct Retired
	{
		std::shared_ptr<const SampleBuffer> sample;
		std::uint64_t at = 0;
	};
	std::vector<Retired> mRetired;

	/** Incremented at the top of every process() call.

	    This is the whole lock-free handoff: a buffer retired when the
	    counter read N cannot still be in use once the counter has reached
	    N + 2, because process() runs one block at a time and the block
	    that might have held the old pointer must have returned before the
	    next one started. No lock, no allocation on the audio thread, and
	    nothing freed underneath a block that is still reading it. */
	std::atomic<std::uint64_t> mBlockCounter { 0 };

	/** False when the host has deactivated us. Then no block is running,
	    nothing can be holding a retired pointer, and everything on the
	    retire list can go at once. */
	std::atomic<bool> mActive { false };

	Project6Dsp mDsp;

	/** Interleaved stereo scratch, sized in setupProcessing. THE AUDIO
	    THREAD NEVER ALLOCATES: a 64-bit host is served from the same
	    buffer by converting on the way out, rather than by a second
	    allocation discovered halfway through a block. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace Project6
