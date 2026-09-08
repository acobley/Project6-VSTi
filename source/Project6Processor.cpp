//------------------------------------------------------------------------
// Project6 - audio processor implementation
//------------------------------------------------------------------------

#include "Project6Processor.h"
#include "Project6IDs.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstddef>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Project6 {

//------------------------------------------------------------------------
Project6Processor::Project6Processor ()
{
	setControllerClass (kProject6ControllerUID);

	for (int i = 0; i < kNumParams; ++i)
		mParams[i] = kParams[i].defaultNormalized ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// An instrument: notes in, audio out, no audio input.
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
	addEventInput (STR16 ("Event In"), 16);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::terminate ()
{
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                          SpeakerArrangement* outputs, int32 numOuts)
{
	// Stereo out, nothing in. This must agree with the AudioComponents
	// entry in resource/au-info.plist (0 in / 2 out) or auval rejects the
	// AU - and it is the plist, not this function, that is usually the one
	// left with the template's extra layout in it.
	if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);

	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// Hosts offer 64-bit buffers, so both are accepted; renderSegment
	// converts on the way out of the scratch.
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::setupProcessing (ProcessSetup& setup)
{
	mSampleRate = setup.sampleRate;

	// ALLOCATION BELONGS HERE, not in process(). Both the SpaceDub and the
	// ForTran DXis reallocated from inside their processing loops.
	mDsp.setSampleRate (mSampleRate);
	mScratch.assign (static_cast<size_t> (setup.maxSamplesPerBlock) * kChannelCount, 0.f);

	return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::setActive (TBool state)
{
	if (state)
	{
		mDsp.reset ();
		sendSampleRateToController ();
	}
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
void Project6Processor::sendSampleRateToController ()
{
	// setActive is [UI-thread], so a message is legitimate here. It would
	// NOT be from process(): the host's connection proxy discards those.
	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kProject6SampleRateMessage);
		message->getAttributes ()->setFloat (kProject6SampleRateAttribute, mSampleRate);
		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void Project6Processor::applyParameterChanges (IParameterChanges* changes)
{
	if (!changes)
		return;

	const int32 count = changes->getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (!queue)
			continue;

		const int32 points = queue->getPointCount ();
		if (points <= 0)
			continue;

		// The value at the end of the block is the target; the trim's own
		// smoother is what makes the move gradual.
		int32      sampleOffset = 0;
		ParamValue value        = 0.0;
		if (queue->getPoint (points - 1, sampleOffset, value) != kResultTrue)
			continue;

		const ParamID id = queue->getParameterId ();
		if (id == kBypass)
		{
			mBypass = (value >= 0.5);
			continue;
		}

		// RANGE-CHECK BEFORE INDEXING: kBypass is 1000, far past the array.
		if (id < kNumParams)
			mParams[id] = value;
	}
}

//------------------------------------------------------------------------
void Project6Processor::handleEvent (const Event& event)
{
	switch (event.type)
	{
		case Event::kNoteOnEvent:
		case Event::kNoteOffEvent:
			// Nothing sounds yet. The events are still walked, at their
			// own sample offsets, so the synthesis drops into a path that
			// is already sample-accurate rather than one bolted on later.
			break;

		default:
			break;
	}
}

//------------------------------------------------------------------------
void Project6Processor::renderSegment (ProcessData& data, int32 offset, int32 numSamples)
{
	if (numSamples <= 0)
		return;

	const size_t needed = static_cast<size_t> (numSamples) * kChannelCount;
	if (mScratch.size () < needed)
		return;                   // setupProcessing sized this; NEVER grow it here

	mDsp.render (mScratch.data (), numSamples);

	// Bypass on an instrument means "make no sound", there being no input
	// to pass through. Events are still handled either way, so no note can
	// hang behind a bypass switch.
	if (mBypass)
		std::fill_n (mScratch.begin (), needed, 0.f);

	AudioBusBuffers& out = data.outputs[0];
	const bool isDouble = (processSetup.symbolicSampleSize == kSample64);

	for (int32 ch = 0; ch < out.numChannels; ++ch)
	{
		// A host may hand us more channels than the DSP fills; the last
		// one it does fill is repeated rather than leaving them undefined.
		const int32 src = (ch < kChannelCount) ? ch : kChannelCount - 1;
		if (isDouble)
		{
			if (!out.channelBuffers64 || !out.channelBuffers64[ch]) continue;
			Sample64* dst = out.channelBuffers64[ch] + offset;
			for (int32 i = 0; i < numSamples; ++i)
				dst[i] = mScratch[static_cast<size_t> (i) * kChannelCount + src];
		}
		else
		{
			if (!out.channelBuffers32 || !out.channelBuffers32[ch]) continue;
			Sample32* dst = out.channelBuffers32[ch] + offset;
			for (int32 i = 0; i < numSamples; ++i)
				dst[i] = mScratch[static_cast<size_t> (i) * kChannelCount + src];
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::process (ProcessData& data)
{
	applyParameterChanges (data.inputParameterChanges);

	mDsp.setOutputTrimDb (paramDef (kOutputTrim).toInternal (mParams[kOutputTrim]));

	// A PARAMETER-ONLY BLOCK: numSamples == 0, or no output bus at all.
	// Hosts send these, and the validator sends them deliberately. Consume
	// the events anyway so a note-off is never dropped.
	if (data.numOutputs <= 0 || data.outputs == nullptr || data.numSamples <= 0)
	{
		if (auto* events = data.inputEvents)
		{
			const int32 count = events->getEventCount ();
			for (int32 i = 0; i < count; ++i)
			{
				Event e;
				if (events->getEvent (i, e) == kResultOk)
					handleEvent (e);
			}
		}
		return kResultOk;
	}

	// Sample-accurate: render up to each event, apply it, carry on.
	int32 position = 0;
	if (auto* events = data.inputEvents)
	{
		const int32 count = events->getEventCount ();
		for (int32 i = 0; i < count; ++i)
		{
			Event e;
			if (events->getEvent (i, e) != kResultOk)
				continue;

			const int32 at = std::clamp (e.sampleOffset, position, data.numSamples);
			renderSegment (data, position, at - position);
			position = at;
			handleEvent (e);
		}
	}
	renderSegment (data, position, data.numSamples - position);

	// Nothing is sounding, and saying so lets the host skip downstream
	// work. WHEN VOICES ARRIVE THIS MUST BECOME CONDITIONAL - a synth that
	// flags silence while a note is playing is silenced by the host.
	data.outputs[0].silenceFlags =
	    (data.outputs[0].numChannels >= 64)
	        ? ~0ULL
	        : ((1ULL << data.outputs[0].numChannels) - 1);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::getState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	streamer.writeInt32 (1);              // stream version
	streamer.writeInt32 (kNumStoredParams);
	for (ParamID id = 0; id < kNumStoredParams; ++id)
		streamer.writeDouble (mParams[id]);
	streamer.writeInt32 (mBypass ? 1 : 0);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::setState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return kResultFalse;

	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return kResultFalse;
		if (i < static_cast<int32> (kNumStoredParams))
			mParams[i] = std::min (1.0, std::max (0.0, v));
	}

	// A project saved before a parameter existed carries a SHORTER stream.
	// Everything it does not mention goes back to its DEFAULT rather than
	// keeping whatever the previous patch left in this instance - loading
	// an old project after a new one must not inherit the new one's
	// settings. The controller's setComponentState does the identical
	// thing, from the identical layout.
	for (int32 i = count; i < static_cast<int32> (kNumStoredParams); ++i)
		mParams[i] = kParams[i].defaultNormalized ();

	int32 bypass = 0;
	mBypass = false;
	if (streamer.readInt32 (bypass))
		mBypass = (bypass != 0);

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Project6
