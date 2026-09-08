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

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

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

	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);
	void sendSampleRateToController ();
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

	Project6Dsp mDsp;

	/** Interleaved stereo scratch, sized in setupProcessing. THE AUDIO
	    THREAD NEVER ALLOCATES: a 64-bit host is served from the same
	    buffer by converting on the way out, rather than by a second
	    allocation discovered halfway through a block. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace Project6
