//------------------------------------------------------------------------
// Project6 - audio processor
//
// Project6 is an INSTRUMENT: an event input and stereo audio outputs, no
// audio input. That decision reaches four places and they must agree -
// PlugType::kInstrumentSynth in Project6Entry.cpp, the buses added in
// initialize(), what setBusArrangements accepts, and the 0-in / 2-out
// entry in resource/au-info.plist. auval checks the last two against each
// other.
//
// NINE OUTPUT BUSES: the main mix, and one AUX bus per row carrying that
// row's DIRECT OUT - its pads summed, tapped before the row fader. See
// Project6Dsp.h for where in the chain that is, and docs/routing.png for
// the picture.
//
// The DSP renders silence. What is here is the bus layout, the parameter
// plumbing, the event handling and the state - which is exactly what the
// SDK validator exercises, and getting it validating before there is any
// audio to blame is the point of doing it in this order.
//
// THE PROCESS CONTEXT IS ASKED FOR, and this is the note the scaffold
// left about why it has to be. Since VST3 3.7 the ProcessContext is
// OPT-IN and the default is NO FLAGS: without the declaration in
// getProcessContextRequirements below, data.processContext arrives with
// nothing valid in it, everything that reads the tempo silently gets 120
// in every host, and the validator prints "- None" rather than
// complaining. A plug-in that launches on the bar would simply never
// launch, and nothing would say why.
//
// AudioEffect already implements IProcessContextRequirements; all that is
// needed is to say which fields we read.
//------------------------------------------------------------------------

#pragma once

#include "Project6Dsp.h"
#include "Project6Midi.h"
#include "Project6Params.h"
#include "Project6Sample.h"
#include "Project6Slots.h"
#include "Project6Transport.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <atomic>
#include <cstddef>
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

	/** Which fields of the ProcessContext this plug-in reads. See the
	    banner: without this they all arrive invalid. */
	Steinberg::uint32 PLUGIN_API getProcessContextRequirements () SMTG_OVERRIDE;

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
	void publishSlot (int index, std::shared_ptr<const SampleBuffer> sample,
	                  std::shared_ptr<const MidiClip> clip);

	/** Free the retired buffers it is now safe to free. `force` is for
	    teardown, where there is no audio thread left to wait for. */
	void collectRetired (bool force);

	void sendSlotStatusToController (int index);
	void sendAllSlotStatusesToController ();
	/** Note-on / note-off at one sample offset. Nothing sounds yet; the
	    events are consumed anyway so that when something does, the
	    sample-accurate path underneath it is already the one being used. */
	void handleEvent (const Steinberg::Vst::Event& event);
	/** Render `numSamples` into the interleaved scratches, then copy each
	    to its bus at `offset`. */
	void renderSegment (Steinberg::Vst::ProcessData& data, Steinberg::int32 offset,
	                    Steinberg::int32 numSamples);

	/** De-interleave one scratch onto one output bus, in whichever sample
	    size the host asked for.

	    A bus the host has not given us - deactivated, or simply not asked
	    for - is skipped rather than assumed. Nine buses is eight more
	    chances for that to happen than this plug-in used to have. */
	void writeBus (Steinberg::Vst::ProcessData& data, Steinberg::int32 busIndex,
	               const float* interleaved, Steinberg::int32 offset,
	               Steinberg::int32 numSamples);

	//--------------------------------------------------------------------
	// The transport, and launching on the bar
	//--------------------------------------------------------------------

	/** Copy the host's ProcessContext into something the SDK-free bar
	    clock can work with. */
	TransportInfo readTransport (const Steinberg::Vst::ProcessData& data) const;

	/** A grid line has arrived at step `step` of the bar: whatever is
	    armed on a slot whose division fires there becomes whatever plays.

	    Step 0 is the bar line and EVERY division fires on it, so
	    applyGridLine(0) is "apply everything" - which is what the
	    degraded paths, where there is no grid to speak of, ask for.

	    IDEMPOTENT, deliberately. It only acts on slots whose armed state
	    differs from what is launched, so a line that arrives twice - a
	    host repeating a block, a cycle wrapping onto the same line -
	    cannot restart a pad that is already running. */
	void applyGridLine (int step, Steinberg::int32 sampleOffset, double blockStartPpq,
	                    double quartersPerSample);

	/** The transport is not rolling: silence every voice but leave the
	    arming alone, so rolling again brings the same pads back in. */
	void silenceForTransport ();

	//--------------------------------------------------------------------
	// MIDI out
	//--------------------------------------------------------------------

	/** Run every launched MIDI pad across this block and queue what it
	    produces. Called once, for the whole block, AFTER the audio has
	    been rendered - the events carry their own sample offsets, so they
	    do not need to be interleaved with the rendering the way a bar
	    line does. */
	void renderMidi (const TransportInfo& transport, Steinberg::int32 numSamples);

	/** Put one pad's events on the queue, tagged with its row. Silently
	    drops anything past kMaxBlockMidiEvents - see the queue's own
	    comment for why that bound is safe. */
	void queueMidi (int row, const MidiEventOut* events, int count);

	/** Stop one MIDI pad AT a sample offset, queueing the note-offs.

	    Every way a pad can go quiet ends up here: stopped at a grid line,
	    the transport stopping, the file taken away, the plug-in
	    bypassed. A hanging note is the failure mode of every MIDI looper,
	    and one function is how each of those becomes one call rather than
	    one more thing to remember. */
	void stopMidiVoice (int slot, Steinberg::int32 sampleOffset);

	/** The same, AND the pad is no longer launched.

	    TWO FUNCTIONS BECAUSE THE DIFFERENCE IS THE BUG. applyGridLine
	    only acts on a slot whose armed state differs from what is
	    launched, so a pad stopped by the first of these and left launched
	    is in a state no grid line will ever act on again: silent for
	    ever, while the transport rolls on and the panel goes on saying it
	    is armed.

	    Use this one wherever the pad should come back when the reason it
	    stopped goes away - a bypass, a transport stop. Use the plain one
	    only where the pad is deliberately being held launched. */
	void unlaunchMidiVoice (int slot, Steinberg::int32 sampleOffset);

	/** Sort the queue into sample order and hand it to the host, on the
	    merged bus AND on the row's own. */
	void flushMidi (Steinberg::Vst::ProcessData& data);

	/** Quarter notes per sample, from the host's tempo. Zero when the
	    host has not given us a musical context, which is the one case in
	    which a MIDI pad cannot play at all. */
	double quartersPerSample (const TransportInfo& transport) const;

	/** Push the published values into data.outputParameterChanges, but
	    ONLY the ones that have moved. Sixty-six queues a block, most of
	    them saying what they said last time, is work the host has to do
	    for nothing. */
	void publishLiveValues (Steinberg::Vst::ProcessData& data);
	void publishOne (Steinberg::Vst::IParameterChanges* changes,
	                 Steinberg::Vst::ParamID id, double normalized);

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

	//--------------------------------------------------------------------
	// The MIDI side of a slot
	//
	// A pad holds EITHER a .wav or a .mid, so exactly one of mSamples and
	// mClips is filled for any given slot. The MIDI half deliberately
	// mirrors the audio half rather than inventing a second discipline:
	// owned here as a shared_ptr, published to the audio thread as a bare
	// pointer through an atomic, and retired on the same list against the
	// same block counter.
	//
	// WHAT DOES NOT MIRROR IT is where the playing happens. Audio is
	// rendered by Project6Dsp, which owns its voices; MIDI is sequenced
	// HERE, because what it produces is not samples in a buffer but
	// events in the host's own list, and handing that back through the
	// DSP would be a layer that carried nothing.
	//--------------------------------------------------------------------

	std::shared_ptr<const MidiClip> mClips[kSlotCount];

	/** What the AUDIO THREAD reads. Release-ordered on the way in, like
	    Project6Dsp::setSlotSample, so a thread that sees the pointer also
	    sees the notes behind it. */
	std::atomic<const MidiClip*> mLiveClips[kSlotCount];

	/** One playhead per pad. Touched by the audio thread only. */
	MidiVoice mMidiVoices[kSlotCount];

	/** Which of the two a slot is holding, so nothing has to test the
	    extension a second time. */
	SlotFileKind mKind[kSlotCount] = {};

	/** Set by the UI thread when a slot's file changed KIND, cleared by
	    the audio thread when it has acted on it. The two kinds are played
	    by different machinery, so a pad that changed from one to the
	    other has to be stopped and relaunched rather than handed over. */
	std::atomic<bool> mRelaunch[kSlotCount] = {};

	/** How many events one BLOCK may carry, across all sixty-four pads.

	    A bound rather than a budget, and the reason the queue below is a
	    member and not a local: sixty-four stack arrays of 192 events each
	    would be most of a megabyte on the audio thread's stack. */
	static constexpr int kMaxBlockMidiEvents = 512;

	/** One event, and which row's bus it belongs on. */
	struct PendingMidi
	{
		int          row = 0;
		MidiEventOut event;
	};

	/** COLLECTED ACROSS THE BLOCK AND SORTED AT THE END. Events are
	    produced in two places - a pad stopping at a grid line, part way
	    through the block, and a pad's own sequencing, run for the whole
	    block afterwards - so they arrive out of order. A host is entitled
	    to an event list in sample order and some drop the whole list
	    rather than sort it. */
	PendingMidi mMidiQueue[kMaxBlockMidiEvents];
	int mMidiQueued = 0;

	/** How each slot's file read, so the panel can say why one will not
	    play. */
	SampleStatus mStatus[kSlotCount] = {};

	/** A buffer that has been replaced, and the block number at which it
	    was replaced. */
	struct Retired
	{
		std::shared_ptr<const SampleBuffer> sample;
		/** ...or the clip, when it was a MIDI pad. ONE LIST FOR BOTH:
		    they are retired against the same block counter for the same
		    reason, and two lists would be two chances to get the +2 wrong
		    in different ways. */
		std::shared_ptr<const MidiClip> clip;
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

	//--------------------------------------------------------------------
	// Arming
	//
	// A CLICK NO LONGER STARTS ANYTHING. mArmed is what the user has asked
	// for, straight off the trigger parameters; mLaunched is what the DSP
	// has actually been told. The bar line is the only thing that copies
	// one into the other, which is the whole feature in one sentence.
	//--------------------------------------------------------------------
	bool mArmed[kSlotCount] = {};
	bool mLaunched[kSlotCount] = {};

	/** Which grid line each slot is waiting for, read off its parameter
	    once a block. */
	LaunchDivision mDivision[kSlotCount] = {};

	BarClock mBarClock;
	bool mWasPlaying = false;

	/** How many blocks in a row the host has rolled without telling us
	    where it is. One or two is a resync and is held through; a run of
	    them means the notes have to be let go of - see renderMidi. About
	    a third of a second at a typical buffer size. */
	static constexpr int kMidiStarveBlocks = 32;
	int mMidiStarved = 0;

	/** What was last sent to the controller, so only changes are sent.
	    NaN so the first block publishes everything. */
	double mPublished[kNumParams];

	Project6Dsp mDsp;

	/** Interleaved stereo scratch, sized in setupProcessing. THE AUDIO
	    THREAD NEVER ALLOCATES: a 64-bit host is served from the same
	    buffer by converting on the way out, rather than by a second
	    allocation discovered halfway through a block. */
	std::vector<float> mScratch;

	/** kSlotRows more of the same, one per row's direct out, in one
	    allocation. mRowScratch.data() + row * mRowScratchStride is that
	    row's buffer; the stride is in FLOATS, not frames. */
	std::vector<float> mRowScratch;
	std::size_t mRowScratchStride = 0;
};

//------------------------------------------------------------------------
} // namespace Project6
