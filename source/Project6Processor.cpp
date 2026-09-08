//------------------------------------------------------------------------
// Project6 - audio processor implementation
//------------------------------------------------------------------------

#include "Project6Processor.h"
#include "Project6IDs.h"
#include "Project6SlotState.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/utility/processcontextrequirements.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Project6 {

//------------------------------------------------------------------------
Project6Processor::Project6Processor ()
{
	setControllerClass (kProject6ControllerUID);

	for (ParamID id = 0; id < kNumParams; ++id)
	{
		mParams[id] = paramDef (id).defaultNormalized ();

		// NaN, so that the first block publishes every value however it
		// compares - "different from last time" has to be true when there
		// was no last time.
		mPublished[id] = std::numeric_limits<double>::quiet_NaN ();
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// An instrument: notes in, audio out, no audio input.
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	// ONE AUX BUS PER ROW, carrying that row's direct out. kAux and not
	// kMain: VST3 has exactly one main output and these are extras, and a
	// host reads the distinction to decide what to patch by default.
	//
	// DEFAULT ACTIVE, deliberately. A bus that arrives switched off looks
	// to most people like a bus that is not there, and eight silent
	// entries in a routing menu are cheaper than eight outputs nobody
	// finds. A host can deactivate any of them, and renderSegment copes
	// with that by skipping the bus rather than assuming it.
	static const char16_t* const kRowBusNames[kSlotRows] = {
		STR16 ("Row A"), STR16 ("Row B"), STR16 ("Row C"), STR16 ("Row D"),
		STR16 ("Row E"), STR16 ("Row F"), STR16 ("Row G"), STR16 ("Row H") };

	for (int row = 0; row < kSlotRows; ++row)
		addAudioOutput (kRowBusNames[row], SpeakerArr::kStereo,
		                BusTypes::kAux, BusInfo::kDefaultActive);

	addEventInput (STR16 ("Event In"), 16);

	return kResultOk;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API Project6Processor::getProcessContextRequirements ()
{
	// WITHOUT THIS, ALL OF IT ARRIVES INVALID. Opt-in since VST3 3.7, and
	// the failure is silent: the tempo reads 120 everywhere, the bar lines
	// land in the wrong places or nowhere at all, and the validator says
	// nothing about it.
	//
	// Only what is actually read. kNeedTransportState is the play flag,
	// kNeedProjectTimeMusic the position, and the other two are what a bar
	// is made of.
	processContextRequirements.needTransportState ();
	processContextRequirements.needProjectTimeMusic ();
	processContextRequirements.needTempo ();
	processContextRequirements.needTimeSignature ();

	return AudioEffect::getProcessContextRequirements ();
}

//------------------------------------------------------------------------
TransportInfo Project6Processor::readTransport (const ProcessData& data) const
{
	TransportInfo info;

	const ProcessContext* context = data.processContext;
	if (context == nullptr)
	{
		// THE HOST GAVE US NOTHING. Not an error - some hosts, and some
		// offline renders, simply do not. Gating on a transport we cannot
		// see would make the plug-in silent and look broken, so this is
		// kTransportUnknown, where a pad launches at once.
		return info;
	}

	info.hasContext = true;
	info.playing = (context->state & ProcessContext::kPlaying) != 0;

	const bool haveTempo = (context->state & ProcessContext::kTempoValid) != 0;
	const bool haveSig   = (context->state & ProcessContext::kTimeSigValid) != 0;
	const bool havePos   = (context->state & ProcessContext::kProjectTimeMusicValid) != 0;

	// ALL THREE OR NONE. A tempo without a position, or a position
	// without a time signature, cannot locate a bar - so there is one
	// flag rather than three, and every caller has one thing to check.
	info.musical = haveTempo && haveSig && havePos;

	if (haveTempo)
		info.tempoBpm = context->tempo;
	if (haveSig)
	{
		info.sigNumerator   = context->timeSigNumerator;
		info.sigDenominator = context->timeSigDenominator;
	}
	if (havePos)
		info.ppq = context->projectTimeMusic;

	return info;
}

//------------------------------------------------------------------------
void Project6Processor::applyGridLine (int step)
{
	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		// NOT THIS SLOT'S LINE. A slot set to a whole bar ignores the
		// seven lines in between; one set to an eighth takes them all.
		// Step 0 is the bar line and every division fires on it, which
		// is what keeps a 1/1 slot and a 1/8 slot in phase.
		if (!divisionFires (mDivision[slot], step))
			continue;

		// ONLY WHAT CHANGED. A line that arrives twice - a host repeating
		// a block, a cycle wrapping straight back onto it - must not
		// restart a pad that is already running, and a pad that is
		// free-running past its line must be left alone.
		if (mArmed[slot] == mLaunched[slot])
			continue;

		mLaunched[slot] = mArmed[slot];
		mDsp.setSlotPlaying (slot, mLaunched[slot]);
	}
}

//------------------------------------------------------------------------
void Project6Processor::silenceForTransport ()
{
	// THE ARMING IS LEFT ALONE. A pad stays lit while the transport is
	// stopped and comes back in on the next bar line when it rolls again,
	// from the top of its sample - which is what makes a rewind something
	// you can do without re-clicking eight pads.
	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		if (!mLaunched[slot])
			continue;

		mLaunched[slot] = false;
		mDsp.setSlotPlaying (slot, false);
	}
}

//------------------------------------------------------------------------
void Project6Processor::publishOne (IParameterChanges* changes, ParamID id, double normalized)
{
	// Only what moved. `!=` is deliberate on a double seeded with NaN:
	// NaN compares unequal to everything including itself, which is
	// exactly the "there was no last time" behaviour wanted here.
	if (!(mPublished[id] != normalized))
		return;

	int32 index = 0;
	if (auto* queue = changes->addParameterData (id, index))
	{
		int32 point = 0;
		queue->addPoint (0, normalized, point);
		mPublished[id] = normalized;
	}
}

//------------------------------------------------------------------------
void Project6Processor::publishLiveValues (ProcessData& data)
{
	IParameterChanges* changes = data.outputParameterChanges;
	if (changes == nullptr)
		return;                 // a host that does not forward these; the panel copes

	const TransportInfo info = readTransport (data);

	const int state = !info.hasContext ? kTransportUnknown
	                                   : (info.playing ? kTransportPlaying : kTransportStopped);

	publishOne (changes, kLiveTransport,
	            liveTransportDef ().toNormalized (static_cast<double> (state)));

	// The bar phase is QUANTISED before it is published. Left raw it
	// changes every single block, and would fill the host's queue with
	// hundreds of points a second to move a playhead by two pixels.
	const double phase = info.musical ? barPhase (info) : 0.0;
	publishOne (changes, kLiveBarPhase, std::floor (phase * 128.0) / 128.0);

	const int beats = info.musical
		? std::min (kMaxBeatsPerBar, std::max (1, info.sigNumerator))
		: 4;
	publishOne (changes, kLiveBeatsPerBar,
	            liveBeatsPerBarDef ().toNormalized (static_cast<double> (beats)));

	for (int slot = 0; slot < kSlotCount; ++slot)
		publishOne (changes, liveSlotParam (slot), mDsp.slotSounding (slot) ? 1.0 : 0.0);
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::terminate ()
{
	// Nothing is rendering by the time terminate is reached, so the
	// retire list can go without waiting for the block counter.
	mActive.store (false, std::memory_order_release);
	for (int index = 0; index < kSlotCount; ++index)
		mDsp.setSlotSample (index, nullptr);
	collectRetired (true);

	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                          SpeakerArrangement* outputs, int32 numOuts)
{
	// Stereo out, nothing in - on the main bus and on all eight row
	// buses. This must agree with the AudioComponents entry in
	// resource/au-info.plist, which describes the MAIN element as 0 in /
	// 2 out, or auval rejects the AU.
	if (numIns != 0 || numOuts != 1 + kSlotRows)
		return kResultFalse;

	for (int32 i = 0; i < numOuts; ++i)
		if (outputs[i] != SpeakerArr::kStereo)
			return kResultFalse;

	return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
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

	// The row buses need a scratch of their own, and this is the only
	// place the host tells anyone how big a block to expect.
	mDsp.setMaxBlockSize (setup.maxSamplesPerBlock);

	mScratch.assign (static_cast<size_t> (setup.maxSamplesPerBlock) * kChannelCount, 0.f);

	// One allocation for all eight row taps, indexed by stride. Sized
	// here and never resized on the audio thread.
	mRowScratchStride = static_cast<size_t> (setup.maxSamplesPerBlock) * kChannelCount;
	mRowScratch.assign (mRowScratchStride * kSlotRows, 0.f);

	// setSampleRate resets the DSP, so nothing is playing any more and
	// mLaunched must say so - otherwise applyBarLine sees "already
	// launched" and never starts the pads again. The ARMING survives: the
	// user has not un-clicked anything.
	for (bool& launched : mLaunched)
		launched = false;
	mBarClock.reset ();
	mWasPlaying = false;

	return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::setActive (TBool state)
{
	if (state)
	{
		mDsp.reset ();

		// Same reason as in setupProcessing: reset() stopped every voice.
		for (bool& launched : mLaunched)
			launched = false;
		mBarClock.reset ();
		mWasPlaying = false;

		mActive.store (true, std::memory_order_release);
		sendSampleRateToController ();

		// EVERY slot's status, every time we are activated. A panel
		// opened after the file was loaded, or a project restored before
		// the two components were connected, has no other way to learn
		// that a slot failed - and a slot that will not play and will not
		// say why is worse than one that was never filled.
		sendAllSlotStatusesToController ();
	}
	else
	{
		// No block can be running once we are inactive, so everything on
		// the retire list is safe to free.
		mActive.store (false, std::memory_order_release);
		collectRetired (true);
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
tresult PLUGIN_API Project6Processor::notify (IMessage* message)
{
	if (message && FIDStringsEqual (message->getMessageID (),
	                                kProject6ProgressRequestMessage))
	{
		// notify() is [UI-thread], so replying with a message is
		// legitimate here - it would not be from process().
		//
		// The values themselves were written by the AUDIO thread, one
		// relaxed atomic store per voice per block. Nothing is locked and
		// nothing waits: a bar drawn from a value one block old is right
		// to within eleven milliseconds.
		float progress[kSlotCount];
		for (int slot = 0; slot < kSlotCount; ++slot)
			progress[slot] = mDsp.slotProgress (slot);

		if (auto* reply = allocateMessage ())
		{
			FReleaser releaser (reply);
			reply->setMessageID (kProject6ProgressDataMessage);
			reply->getAttributes ()->setBinary (
				kProject6ProgressAttribute, progress,
				static_cast<uint32> (sizeof (progress)));
			sendMessage (reply);
		}
		return kResultOk;
	}

	if (message && FIDStringsEqual (message->getMessageID (), kProject6SlotMessage))
	{
		int64 index = -1;
		if (message->getAttributes ()->getInt (kProject6SlotIndexAttribute, index)
		        != kResultOk)
			return kResultOk;

		// An ABSENT path attribute is a cleared slot, not a failure - see
		// the note in Project6IDs.h. The empty string that leaves here is
		// what SlotBank stores for an empty slot.
		std::string path;
		const void* data = nullptr;
		uint32 size = 0;
		if (message->getAttributes ()->getBinary (kProject6SlotPathAttribute, data, size)
		            == kResultOk
		    && data != nullptr && size > 0)
		{
			path.assign (static_cast<const char*> (data), size);
		}

		// setPath range-checks the index itself and refuses a bad one
		// rather than clamping, so a message from a future build with a
		// bigger grid is dropped instead of overwriting slot 63.
		if (mSlots.setPath (static_cast<int> (index), path))
		{
			// notify() is [UI-thread], which is the only reason this is
			// allowed to open a file at all.
			loadSlot (static_cast<int> (index));
		}
		return kResultOk;
	}

	return AudioEffect::notify (message);
}

//------------------------------------------------------------------------
void Project6Processor::loadSlot (int index)
{
	if (!isSlotIndex (index))
		return;

	const std::string& path = mSlots.path (index);

	SampleBuffer buffer;
	const SampleStatus status = path.empty () ? SampleStatus::Empty
	                                          : loadWavFile (path, buffer);

	// A FAILED LOAD STILL KEEPS THE PATH. The slot goes on showing the
	// file's name and the project goes on remembering it - the file may
	// simply be on a drive that is not plugged in today. What it does not
	// get is audio, and the status is how the panel says so.
	std::shared_ptr<const SampleBuffer> sample;
	if (status == SampleStatus::Loaded)
		sample = std::make_shared<const SampleBuffer> (std::move (buffer));

	mStatus[index] = status;
	publishSlot (index, std::move (sample));
	sendSlotStatusToController (index);
}

//------------------------------------------------------------------------
void Project6Processor::loadAllSlots ()
{
	for (int index = 0; index < kSlotCount; ++index)
		loadSlot (index);
}

//------------------------------------------------------------------------
void Project6Processor::publishSlot (int index, std::shared_ptr<const SampleBuffer> sample)
{
	if (!isSlotIndex (index))
		return;

	std::shared_ptr<const SampleBuffer> previous = std::move (mSamples[index]);
	mSamples[index] = std::move (sample);

	// The DSP gets a bare pointer into a buffer this object owns. It is
	// only ever read there, and never freed there.
	mDsp.setSlotSample (index, mSamples[index] ? mSamples[index].get () : nullptr);

	// The counter is read AFTER the swap, so any block still holding the
	// old pointer started at or before this value.
	if (previous)
		mRetired.push_back (
			{ std::move (previous), mBlockCounter.load (std::memory_order_acquire) });

	collectRetired (false);
}

//------------------------------------------------------------------------
void Project6Processor::collectRetired (bool force)
{
	const std::uint64_t now = mBlockCounter.load (std::memory_order_acquire);
	const bool inactive = !mActive.load (std::memory_order_acquire);

	mRetired.erase (
		std::remove_if (mRetired.begin (), mRetired.end (),
		                [&] (const Retired& retired)
		                {
			                // +2, not +1: the block that was running when
			                // the swap happened may have started at `at`
			                // itself, so one further increment only proves
			                // that block began, not that it finished.
			                return force || inactive || now >= retired.at + 2;
		                }),
		mRetired.end ());
}

//------------------------------------------------------------------------
void Project6Processor::sendSlotStatusToController (int index)
{
	if (!isSlotIndex (index))
		return;

	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kProject6SlotStatusMessage);
		message->getAttributes ()->setInt (kProject6SlotIndexAttribute, index);
		message->getAttributes ()->setInt (kProject6SlotStatusAttribute,
		                                   static_cast<int64> (mStatus[index]));
		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void Project6Processor::sendAllSlotStatusesToController ()
{
	for (int index = 0; index < kSlotCount; ++index)
		sendSlotStatusToController (index);
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
void Project6Processor::writeBus (ProcessData& data, int32 busIndex,
                                  const float* interleaved, int32 offset, int32 numSamples)
{
	// A BUS THE HOST DID NOT GIVE US. Deactivated, or a host that simply
	// asked for fewer - either way it is skipped rather than assumed.
	if (busIndex >= data.numOutputs || interleaved == nullptr)
		return;

	AudioBusBuffers& out = data.outputs[busIndex];
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
				dst[i] = interleaved[static_cast<size_t> (i) * kChannelCount + src];
		}
		else
		{
			if (!out.channelBuffers32 || !out.channelBuffers32[ch]) continue;
			Sample32* dst = out.channelBuffers32[ch] + offset;
			for (int32 i = 0; i < numSamples; ++i)
				dst[i] = interleaved[static_cast<size_t> (i) * kChannelCount + src];
		}
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

	// The eight row taps. A row whose bus the host did not give us gets a
	// null pointer and the DSP skips it - there is no point summing into
	// a buffer nobody will read.
	float* rowOuts[kSlotRows] = { nullptr };
	const bool haveRowScratch = (mRowScratchStride >= needed
	                             && mRowScratch.size () >= mRowScratchStride * kSlotRows);
	if (haveRowScratch)
	{
		for (int row = 0; row < kSlotRows; ++row)
			if (1 + row < data.numOutputs)
				rowOuts[row] = mRowScratch.data () + mRowScratchStride * row;
	}

	mDsp.render (mScratch.data (), haveRowScratch ? rowOuts : nullptr, numSamples);

	// Bypass on an instrument means "make no sound", there being no input
	// to pass through - and that has to include the direct outs, or a
	// bypassed plug-in would still be feeding the desk. Events are still
	// handled either way, so no note can hang behind a bypass switch.
	if (mBypass)
	{
		std::fill_n (mScratch.begin (), needed, 0.f);
		for (int row = 0; row < kSlotRows; ++row)
			if (rowOuts[row] != nullptr)
				std::fill_n (rowOuts[row], needed, 0.f);
	}

	writeBus (data, 0, mScratch.data (), offset, numSamples);

	for (int row = 0; row < kSlotRows; ++row)
		writeBus (data, 1 + row, rowOuts[row], offset, numSamples);
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::process (ProcessData& data)
{
	// FIRST, and before anything can read a published sample pointer.
	// This is what the retire list waits on - see collectRetired.
	mBlockCounter.fetch_add (1, std::memory_order_acq_rel);

	applyParameterChanges (data.inputParameterChanges);

	mDsp.setOutputTrimDb (paramDef (kOutputTrim).toInternal (mParams[kOutputTrim]));

	//--------------------------------------------------------------------
	// WHAT HAS BEEN ASKED FOR, which is no longer what is playing.
	//
	// A click sets a trigger parameter, and that is all it does. The bar
	// line is what turns armed into launched - see applyBarLine.
	//
	// The LEVELS go straight through, though. A level is not something to
	// wait a bar for - it is a balance, and holding one back until the
	// next bar would make the panel feel broken under the hand. The DSP
	// smooths it, so passing it every block costs nothing.
	//--------------------------------------------------------------------
	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		mArmed[slot] = mParams[slotPlayParam (slot)] >= 0.5;

		mDsp.setSlotLevelDb (
			slot, slotLevelDef ().toInternal (mParams[slotLevelParam (slot)]));

		// Which line this slot is waiting for. Read every block rather
		// than tracked: sixty-four conversions is nothing, and a cached
		// copy would be one more thing to get out of step.
		mDivision[slot] = divisionFromIndex (static_cast<int> (
			slotDivisionDef ().toInternal (mParams[slotDivisionParam (slot)])));
	}

	// And the eight row buses, the same way.
	for (int row = 0; row < kSlotRows; ++row)
		mDsp.setRowLevelDb (row, rowLevelDef ().toInternal (mParams[rowLevelParam (row)]));

	const TransportInfo transport = readTransport (data);

	//--------------------------------------------------------------------
	// The three levels of knowledge, degrading separately.
	//--------------------------------------------------------------------
	if (!transport.hasContext)
	{
		// The host told us nothing. Launch at once, as this plug-in did
		// before it had a transport at all: a sampler that is silent in a
		// host reporting no transport looks broken, not careful. Step 0,
		// because every division fires on the bar line and there is no
		// grid here to be finer about.
		applyGridLine (0);
	}
	else if (!transport.playing)
	{
		// STOPPED. Silence the voices and leave the arming alone, so the
		// pads come back in on the next bar line when the transport rolls
		// again. Resetting the clock is what makes that bar line fire
		// even when it is the very one that was fired before the stop.
		mBarClock.reset ();
		silenceForTransport ();
	}
	else if (!transport.musical)
	{
		// Rolling, but the host cannot say where the bars are - so there
		// are no divisions either. Launching as soon as it rolls is
		// closer to what was asked for than never launching at all.
		applyGridLine (0);
	}

	mWasPlaying = transport.playing;

	// A PARAMETER-ONLY BLOCK: numSamples == 0, or no output bus at all.
	// Hosts send these, and the validator sends them deliberately. Consume
	// the events anyway so a note-off is never dropped, and still publish
	// - the panel's transport readout should not freeze on one.
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

		publishLiveValues (data);
		return kResultOk;
	}

	//--------------------------------------------------------------------
	// Where the grid lines fall inside this block. ALL of them, at the
	// finest division, each carrying which step of the bar it is - one
	// list for all sixty-four slots, which is only possible because the
	// divisions nest.
	//--------------------------------------------------------------------
	GridLine gridLines[kMaxGridLinesPerBlock];
	int lineCount = 0;
	if (transport.playing && transport.musical)
	{
		lineCount = mBarClock.gridLinesInBlock (transport, data.numSamples, mSampleRate,
		                                        gridLines, kMaxGridLinesPerBlock);
	}

	//--------------------------------------------------------------------
	// SAMPLE-ACCURATE, and for two kinds of thing at once: render up to
	// whichever comes first - an event or a bar line - act on it, carry
	// on. A pad launched at the block boundary instead would be up to
	// eleven milliseconds late, which is audible against a click track
	// and gets worse with the buffer size.
	//--------------------------------------------------------------------
	int32 position = 0;
	int32 eventIndex = 0;
	int lineIndex = 0;

	IEventList* events = data.inputEvents;
	const int32 eventCount = (events != nullptr) ? events->getEventCount () : 0;

	for (;;)
	{
		// The next event, if there is one we can read.
		Event event;
		int32 nextEvent = -1;
		while (eventIndex < eventCount)
		{
			if (events->getEvent (eventIndex, event) == kResultOk)
			{
				nextEvent = std::clamp (event.sampleOffset, position, data.numSamples);
				break;
			}
			++eventIndex;
		}

		const int32 nextLine = (lineIndex < lineCount)
			? std::clamp (static_cast<int32> (gridLines[lineIndex].offset),
			              position, data.numSamples)
			: -1;

		if (nextEvent < 0 && nextLine < 0)
			break;

		// The grid line wins a tie: whatever it launches should be heard
		// from that sample, not from the sample after an event that
		// happens to share it.
		const bool takeLine = (nextEvent < 0) || (nextLine >= 0 && nextLine <= nextEvent);
		const int32 at = takeLine ? nextLine : nextEvent;

		renderSegment (data, position, at - position);
		position = at;

		if (takeLine)
		{
			applyGridLine (gridLines[lineIndex].step);
			++lineIndex;
		}
		else
		{
			handleEvent (event);
			++eventIndex;
		}
	}

	renderSegment (data, position, data.numSamples - position);

	// Tell the host when there is genuinely nothing sounding, so it can
	// skip downstream work - and ONLY then. A synth that flags silence
	// while something is playing is silenced by the host, which presents
	// as a pad that lights up and cannot be heard.
	//
	// A voice counts as sounding through its fade-out too, which is why
	// this asks the DSP rather than the parameters.
	//
	// PER BUS. A row's direct out is silent when nothing on that row is
	// sounding, whatever the other seven are doing - which is most of the
	// value of the flag on a nine-bus plug-in, since seven rows out of
	// eight usually are silent.
	const auto allChannels = [] (const AudioBusBuffers& bus) -> uint64
	{
		return (bus.numChannels >= 64) ? ~0ULL
		                               : ((1ULL << bus.numChannels) - 1);
	};

	const bool bypassed = mBypass;

	data.outputs[0].silenceFlags =
	    (bypassed || mDsp.soundingVoiceCount () == 0) ? allChannels (data.outputs[0]) : 0;

	for (int row = 0; row < kSlotRows; ++row)
	{
		const int32 bus = 1 + row;
		if (bus >= data.numOutputs)
			break;

		data.outputs[bus].silenceFlags =
		    (bypassed || !mDsp.rowSounding (row)) ? allChannels (data.outputs[bus]) : 0;
	}

	publishLiveValues (data);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::getState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	streamer.writeInt32 (kStateVersion);
	streamer.writeInt32 (kNumStoredParams);
	for (ParamID id = 0; id < kNumStoredParams; ++id)
		streamer.writeDouble (mParams[id]);
	streamer.writeInt32 (mBypass ? 1 : 0);

	// APPENDED, after everything a version 1 stream held, for the same
	// reason parameters are appended and never inserted: a reader that
	// stops early must stop at a block boundary and not in the middle of
	// something it was half way through understanding.
	writeSlots (streamer, mSlots);
	writeValueBlock (streamer, &mParams[kSlotLevelBase], kSlotCount);
	writeValueBlock (streamer, &mParams[kRowLevelBase], kSlotRows);
	writeValueBlock (streamer, &mParams[kSlotDivisionBase], kSlotCount);

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

	// EVERYTHING back to its default BEFORE anything is read. A project
	// saved before a parameter existed carries a shorter stream, and
	// whatever it does not mention must go back to its default rather
	// than keeping what the previous patch left in this instance -
	// loading an old project after a new one must not inherit the new
	// one's settings. It is also what stops the sixty-four slot
	// triggers, which are deliberately not in the stream at all, from
	// surviving a project load and leaving pads playing.
	//
	// The controller's setComponentState does the identical thing, from
	// the identical layout.
	for (ParamID id = 0; id < kNumParams; ++id)
		mParams[id] = paramDef (id).defaultNormalized ();

	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return kResultFalse;
		if (i < static_cast<int32> (kNumStoredParams))
			mParams[i] = std::min (1.0, std::max (0.0, v));
	}

	int32 bypass = 0;
	mBypass = false;
	if (streamer.readInt32 (bypass))
		mBypass = (bypass != 0);

	// readSlots empties the bank first, so a version 1 stream - which has
	// nothing here - leaves every slot empty rather than inheriting the
	// samples of whatever was loaded before. Its false return is that
	// case and is not an error. The CONTROLLER reads the identical block,
	// through the identical function.
	readSlots (streamer, mSlots);
	readValueBlock (streamer, &mParams[kSlotLevelBase], kSlotCount,
	                slotLevelDef ().defaultNormalized ());
	readValueBlock (streamer, &mParams[kRowLevelBase], kSlotRows,
	                rowLevelDef ().defaultNormalized ());
	readValueBlock (streamer, &mParams[kSlotDivisionBase], kSlotCount,
	                slotDivisionDef ().defaultNormalized ());

	// The paths are back; now read the files. setState is not the audio
	// thread, so this is where sixty-four disk reads belong - and a slot
	// whose file has moved since the project was saved gets a status
	// rather than silence.
	loadAllSlots ();

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Project6
