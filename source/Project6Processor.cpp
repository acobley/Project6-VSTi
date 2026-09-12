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
#include <cstdarg>
#include <cstdlib>
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

	//--------------------------------------------------------------------
	// MIDI OUT: ONE BUS PER ROW, and each event sent to exactly one of
	// them.
	//
	// It used to be nine - a merged bus carrying every row, then one per
	// row - so that a host showing only the first event output could
	// still reach all eight. Every event was therefore sent TWICE, and a
	// log taken inside Reaper showed the consequence: 1202 notes handed
	// over as 2404 events. Every host that matters merges them anyway.
	// Steinberg's own AU wrapper ignores Event::busIndex completely and
	// converts every event in the list to MIDI on its single output; so,
	// on the evidence, does Reaper.
	//
	// So the merge is what the host does, not something to do twice and
	// hope. Eight buses, bus index IS the row, one event each. In a host
	// that flattens them you get every note once with its row on the
	// channel - which is exactly the merged bus, obtained by not fighting
	// for it. In a host with real per-bus routing the rows are separate,
	// as before.
	//
	// The cost, stated plainly: a host that exposes only the FIRST event
	// output now reaches row A alone. That is the price of not sending
	// everything twice to every host that does not.
	//--------------------------------------------------------------------
	static const char16_t* const kRowMidiNames[kSlotRows] = {
		STR16 ("Row A MIDI"), STR16 ("Row B MIDI"), STR16 ("Row C MIDI"),
		STR16 ("Row D MIDI"), STR16 ("Row E MIDI"), STR16 ("Row F MIDI"),
		STR16 ("Row G MIDI"), STR16 ("Row H MIDI") };

	for (int row = 0; row < kSlotRows; ++row)
		addEventOutput (kRowMidiNames[row], 1, BusTypes::kMain, BusInfo::kDefaultActive);

	for (int slot = 0; slot < kSlotCount; ++slot)
		mLiveClips[slot].store (nullptr, std::memory_order_relaxed);

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
	{
		info.tempoBpm   = context->tempo;
		info.tempoKnown = true;
	}
	if (haveSig)
	{
		info.sigNumerator   = context->timeSigNumerator;
		info.sigDenominator = context->timeSigDenominator;
	}
	if (havePos)
	{
		info.ppq = context->projectTimeMusic;
		info.posKnown = true;
	}

	return info;
}

//------------------------------------------------------------------------
void Project6Processor::applyGridLine (int step, int32 sampleOffset, double blockStartPpq,
                                       double perSample)
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

		if (mKind[slot] == SlotFileKind::Midi)
		{
			if (mArmed[slot])
			{
				// A MIDI PAD CANNOT LAUNCH WITHOUT A TIMELINE TO LAUNCH
				// ON, so it is left ARMED AND NOT LAUNCHED rather than
				// marked launched and then found to be silent. It comes
				// in at the first grid line after the host says where it
				// is - which is the state the panel was already showing.
				if (perSample <= 0.0)
					continue;

				// THE PAD'S LOOP STARTS AT THIS GRID LINE'S OWN PROJECT
				// POSITION, not at the top of the block. From then on
				// where it is in its loop is simply how far the project
				// has moved since - so it cannot drift, it survives a
				// tempo change with no arithmetic, and it follows the
				// host when somebody drags the playhead.
				mLaunched[slot] = true;
				mMidiVoices[slot].start (blockStartPpq
				                         + static_cast<double> (sampleOffset) * perSample);
			}
			else
			{
				mLaunched[slot] = false;
				stopMidiVoice (slot, sampleOffset);
			}
		}
		else
		{
			mLaunched[slot] = mArmed[slot];
			mDsp.setSlotPlaying (slot, mLaunched[slot]);
		}
	}
}

//------------------------------------------------------------------------
double Project6Processor::quartersPerSample (const TransportInfo& transport) const
{
	// TEMPO AND POSITION, AND NOT THE TIME SIGNATURE.
	//
	// This asked for `musical` - all three - and that was a bug with a
	// symptom nobody would trace back to it: a host that reports its
	// tempo and its position but does not flag its time signature on
	// every block stopped every MIDI pad in the bank, at what sounded
	// like random, permanently.
	//
	// Placing a note needs the tempo and the position. The signature only
	// decides where a loop is ROUNDED to, and barQuartersFor falls back
	// to 4/4 when it is not given - which is a slightly wrong loop
	// length, not silence.
	//
	// An audio pad can fall back to launching at once, because a sample
	// can be played at the rate it was recorded. A MIDI loop has no such
	// fallback: without a position there is no timeline to place notes
	// on, and inventing one would put every pad out of step with the
	// project as soon as the tempo moved.
	if (!transport.tempoKnown || !transport.posKnown
	    || !(transport.tempoBpm > 0.0) || !(mSampleRate > 0.0))
		return 0.0;

	return transport.tempoBpm / (60.0 * mSampleRate);
}

//------------------------------------------------------------------------
void Project6Processor::openMidiLog ()
{
	if (mMidiLog != nullptr)
		return;

	//--------------------------------------------------------------------
	// TWO WAYS TO ASK FOR IT, and the second is the one that works.
	//
	// An environment variable is the obvious choice and is nearly useless
	// on macOS: `export` in a terminal reaches the processes that
	// terminal starts, and a DAW is started from the Dock, from Finder or
	// from Spotlight, all of which inherit launchd's environment instead.
	// It was set correctly and never arrived. That is a bad diagnostic -
	// one whose failure looks exactly like the thing it was meant to
	// diagnose.
	//
	// So the real trigger is A FILE THAT ALREADY EXISTS. Create
	// ~/p6-midi-log.txt, restart the host, and it fills up; delete it and
	// it stops. The file is its own switch AND its own destination, so
	// there is nothing to remember and nothing to get out of step.
	//
	// It is only ever opened if it is ALREADY THERE, so a plug-in that
	// nobody asked to log never creates anything.
	//--------------------------------------------------------------------
	std::string path;

	if (const char* asked = std::getenv ("PROJECT6_MIDI_LOG"))
	{
		if (asked[0] != '\0')
			path = asked;
	}

	if (path.empty ())
	{
		if (const char* home = std::getenv ("HOME"))
		{
			const std::string candidate = std::string (home) + "/p6-midi-log.txt";

			// EXISTS ALREADY? Opened for reading first, so that asking
			// the question cannot itself answer it.
			if (std::FILE* probe = std::fopen (candidate.c_str (), "r"))
			{
				std::fclose (probe);
				path = candidate;
			}
		}
	}

	if (path.empty ())
		return;

	mMidiLog = std::fopen (path.c_str (), "w");
	mLoggedBlocks = 0;

	if (mMidiLog != nullptr)
	{
		std::fprintf (mMidiLog,
		              "Project6 MIDI log\n"
		              "  columns: blk | playing | ppq | tempo | frames "
		              "| then one line per event\n"
		              "  events:  +sample  on/off  pitch  vel  ch  bus  ppq\n\n");
		std::fflush (mMidiLog);
	}
}

//------------------------------------------------------------------------
void Project6Processor::closeMidiLog ()
{
	if (mMidiLog == nullptr)
		return;

	std::fclose (mMidiLog);
	mMidiLog = nullptr;
}

//------------------------------------------------------------------------
void Project6Processor::logMidi (const char* format, ...)
{
	if (mMidiLog == nullptr)
		return;

	va_list args;
	va_start (args, format);
	std::vfprintf (mMidiLog, format, args);
	va_end (args);
}

//------------------------------------------------------------------------
void Project6Processor::queueMidi (int row, const MidiEventOut* events, int count)
{
	for (int i = 0; i < count; ++i)
	{
		if (mMidiQueued < kMaxBlockMidiEvents)
		{
			mMidiQueue[mMidiQueued++] = { row, events[i] };
			continue;
		}

		// THE QUEUE IS FULL, AND THE TWO KINDS OF EVENT ARE NOT EQUAL.
		//
		// A dropped note-ON is a note nobody hears - a hole in one bar.
		// A dropped note-OFF is a note nobody can stop: the voice that
		// emitted it has already decremented its own count, so nothing
		// will ever send that off again, and on a synth with a voice
		// limit a few of those is silence while the MIDI goes on
		// arriving. Exactly the symptom this commit is about.
		//
		// So an off displaces the most recently queued ON rather than
		// being thrown away. The sort afterwards puts it back in time
		// order, and the note it displaces is the one least likely to be
		// missed - the last one in.
		if (events[i].noteOn)
			continue;

		for (int at = mMidiQueued - 1; at >= 0; --at)
		{
			if (mMidiQueue[at].event.noteOn)
			{
				mMidiQueue[at] = { row, events[i] };
				break;
			}
		}
	}
}

//------------------------------------------------------------------------
void Project6Processor::stopMidiVoice (int slot, int32 sampleOffset)
{
	if (!isSlotIndex (slot) || !mMidiVoices[slot].playing ())
		return;

	MidiEventOut events[kMaxMidiEventsPerBlock];
	const int count = mMidiVoices[slot].allNotesOff (static_cast<int> (sampleOffset), events,
	                                                 kMaxMidiEventsPerBlock);
	queueMidi (rowOfSlot (slot), events, count);
}

//------------------------------------------------------------------------
void Project6Processor::unlaunchMidiVoice (int slot, int32 sampleOffset)
{
	if (!isSlotIndex (slot))
		return;

	stopMidiVoice (slot, sampleOffset);

	// ONLY A MIDI PAD'S LAUNCH IS CLEARED HERE. This is called in a loop
	// over all sixty-four, and an audio pad that is playing perfectly
	// well must not be unlaunched by a MIDI decision - it would leave the
	// DSP sounding a voice the processor no longer believes in.
	if (mKind[slot] != SlotFileKind::Midi)
		return;

	// AND THE PAD IS NO LONGER LAUNCHED, which is the whole difference
	// between this and the function above, and was the bug.
	//
	// applyGridLine only acts on a slot whose armed state DIFFERS from
	// what is launched. A pad stopped without this line is left armed and
	// launched and not playing - a state no grid line will ever act on
	// again - so it goes quiet for ever while the transport rolls on and
	// the panel goes on saying it is armed. That is what "the MIDI drops
	// out at random and never comes back" was.
	//
	// silenceForTransport has always cleared it, which is why the audio
	// pads recovered from the same situations and the MIDI pads did not.
	mLaunched[slot] = false;
}

//------------------------------------------------------------------------
void Project6Processor::renderMidi (const TransportInfo& transport, int32 numSamples)
{
	const double perSample = quartersPerSample (transport);

	//--------------------------------------------------------------------
	// THE BLOCK HEADER, and it is written HERE rather than beside the
	// events because the block that matters most is the one with NO
	// events in it. A log that only recorded blocks that sent something
	// would fall silent at exactly the moment the plug-in did, and say
	// nothing about why.
	//--------------------------------------------------------------------
	if (mMidiLog != nullptr)
	{
		logMidi ("blk %llu  ctx%d play%d musical%d tempo%d pos%d  ppq %.5f  "
		         "%.2f BPM  %d/%d  frames %d  perSample %.9f\n",
		         static_cast<unsigned long long> (mLoggedBlocks++),
		         transport.hasContext ? 1 : 0, transport.playing ? 1 : 0,
		         transport.musical ? 1 : 0, transport.tempoKnown ? 1 : 0,
		         transport.posKnown ? 1 : 0, transport.ppq, transport.tempoBpm,
		         transport.sigNumerator, transport.sigDenominator,
		         static_cast<int> (numSamples), perSample);

		for (int slot = 0; slot < kSlotCount; ++slot)
		{
			if (mKind[slot] != SlotFileKind::Midi)
				continue;

			logMidi ("   pad %d  armed%d launched%d playing%d loop%d  pos %.4f  "
			         "clip%d\n",
			         slot, mArmed[slot] ? 1 : 0, mLaunched[slot] ? 1 : 0,
			         mMidiVoices[slot].playing () ? 1 : 0, mLoop[slot] ? 1 : 0,
			         mMidiVoices[slot].position (),
			         mLiveClips[slot].load (std::memory_order_acquire) != nullptr ? 1 : 0);
		}
	}

	// BYPASSED OR STOPPED. Both have to stop the notes rather than merely
	// stop producing new ones - a bypass that left a chord sounding would
	// be a bypass you could hear for ever - and both have to UNLAUNCH, so
	// that the next grid line brings the pad back when the reason goes
	// away. See unlaunchMidiVoice.
	if (mBypass || !transport.playing)
	{
		for (int slot = 0; slot < kSlotCount; ++slot)
			unlaunchMidiVoice (slot, 0);
		mMidiStarved = 0;
		return;
	}

	// NO TIMELINE, WHILE ROLLING. Different in kind from the two above:
	// the host is playing and simply has not told us where it is this
	// block. That is usually one block of nothing - a resync, an
	// automation pass - and stopping for it would put an audible hole in
	// every pattern for something nobody could hear.
	//
	// So it HOLDS: nothing is emitted, nothing is stopped, the sounding
	// notes go on sounding, and the moment the context comes back the
	// pads carry on exactly where they were. Only if it persists - long
	// enough that the held notes would be a drone rather than a glitch -
	// are they let go of; the invariant below then notices the pads are
	// not playing and unlaunches them, so the first grid line after the
	// context returns brings them back in on the grid. Either way nobody
	// has to click anything.
	if (perSample <= 0.0)
	{
		if (++mMidiStarved > kMidiStarveBlocks)
		{
			for (int slot = 0; slot < kSlotCount; ++slot)
				stopMidiVoice (slot, 0);
		}
		return;
	}

	mMidiStarved = 0;

	// THE INVARIANT, RE-ASSERTED once a block because getting it wrong is
	// silent and permanent: a pad the processor believes is launched must
	// have a voice that is playing. Anything that stops a voice without
	// unlaunching it - a path added later, a case not thought of - shows
	// up here as one missed bar rather than as a pattern that never comes
	// back.
	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		if (mKind[slot] == SlotFileKind::Midi && mLaunched[slot]
		    && !mMidiVoices[slot].playing ())
		{
			mLaunched[slot] = false;
		}
	}

	// The PROJECT's bar, not the file's: it is the grid the pad launches
	// on, and a loop rounded to any other would drift against everything
	// else on the panel. Read every block, so a time-signature change
	// changes the loop without anything being reloaded.
	const double barQuarters = barQuartersFor (transport.sigNumerator,
	                                           transport.sigDenominator);

	MidiEventOut events[kMaxMidiEventsPerBlock];

	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		if (mKind[slot] != SlotFileKind::Midi || !mMidiVoices[slot].playing ())
			continue;

		// ACQUIRE, to pair with the release store in publishSlot.
		const MidiClip* clip = mLiveClips[slot].load (std::memory_order_acquire);

		const double loop = (clip != nullptr)
			? loopLengthQuarters (clip->content, barQuarters)
			: 0.0;

		const int count = mMidiVoices[slot].render (clip, loop, transport.ppq, perSample,
		                                            static_cast<int> (numSamples),
		                                            mLoop[slot], events,
		                                            kMaxMidiEventsPerBlock);
		queueMidi (rowOfSlot (slot), events, count);
	}
}

//------------------------------------------------------------------------
void Project6Processor::flushMidi (ProcessData& data, double blockStartPpq, double perSample)
{
	if (mMidiQueued <= 0)
		return;

	IEventList* out = data.outputEvents;

	// THE BLIND SPOT THIS LOG USED TO HAVE. A host that hands us no event
	// list at all produced EXACTLY the same log as a plug-in that emitted
	// nothing: silence either way, and no way to tell "we had nothing to
	// say" from "we said it into a hole". Written before the early return,
	// so the hole is what the log shows.
	logMidi ("   flush %d queued, event list %s\n", mMidiQueued,
	         out != nullptr ? "present" : "ABSENT - THE HOST GAVE US NOWHERE TO PUT THEM");

	if (out == nullptr)
	{
		mMidiQueued = 0;
		return;
	}

	// SORTED INTO SAMPLE ORDER. The events were produced in two places -
	// a pad stopping at a grid line part way through the block, and each
	// pad's own sequencing run afterwards - so they arrive out of order,
	// and a host is entitled to a sorted list. STABLE, so a note-off and
	// a note-on at the same sample keep the order they were made in:
	// the off was queued first, and on the same pitch that is the
	// difference between a re-strike and a silence.
	std::stable_sort (mMidiQueue, mMidiQueue + mMidiQueued,
	                  [] (const PendingMidi& a, const PendingMidi& b)
	                  { return a.event.sampleOffset < b.event.sampleOffset; });

	for (int i = 0; i < mMidiQueued; ++i)
	{
		const PendingMidi& pending = mMidiQueue[i];

		Event event = {};
		event.sampleOffset = pending.event.sampleOffset;

		// WHERE THIS EVENT IS IN THE PROJECT, which used to be written as
		// a flat zero on every event ever sent.
		//
		// ppqPosition is documented as "position in project time music".
		// Zero is not a missing value, it is a WRONG one: it says every
		// note this plug-in has ever emitted happened at the very start
		// of the project. A host has no reason to look at it while the
		// transport runs in a straight line - the sample offset is
		// enough - but a LOOPING transport is exactly when a host would
		// consult it, to work out where an event falls relative to the
		// loop region. Which is the one condition under which this was
		// reported to go wrong.
		event.ppqPosition = blockStartPpq
		                    + static_cast<double> (pending.event.sampleOffset) * perSample;

		// AND THEY ARE NOT LIVE. kIsLive means "played live, directly
		// from a keyboard". These were read out of a file and placed on a
		// grid; saying otherwise invites a host to treat them as
		// unsequenced input, which among other things is a reason to
		// ignore the position above.
		event.flags = 0;

		if (pending.event.noteOn)
		{
			event.type = Event::kNoteOnEvent;
			event.noteOn.channel  = static_cast<int16> (midiChannelForRow (pending.row));
			event.noteOn.pitch    = static_cast<int16> (pending.event.note);
			event.noteOn.velocity = static_cast<float> (pending.event.velocity) / 127.f;
			event.noteOn.length   = 0;
			event.noteOn.tuning   = 0.f;
			event.noteOn.noteId   = -1;
		}
		else
		{
			event.type = Event::kNoteOffEvent;
			event.noteOff.channel  = static_cast<int16> (midiChannelForRow (pending.row));
			event.noteOff.pitch    = static_cast<int16> (pending.event.note);
			event.noteOff.velocity = 0.f;
			event.noteOff.tuning   = 0.f;
			event.noteOff.noteId   = -1;
		}

		// ONCE, on this row's bus. See initialize() for why this used to
		// be twice and why it must not be.
		event.busIndex = pending.row;
		const tresult sent = out->addEvent (event);

		logMidi ("   +%-6d %-4s %-4d v%-4d ch%-3d bus%-2d ppq %.5f  %s\n",
		         pending.event.sampleOffset, pending.event.noteOn ? "ON" : "off",
		         static_cast<int> (pending.event.note),
		         static_cast<int> (pending.event.velocity),
		         midiChannelForRow (pending.row) + 1, pending.row, event.ppqPosition,
		         sent == kResultOk ? "ok" : "REFUSED");
	}

	mMidiQueued = 0;
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

		if (mKind[slot] == SlotFileKind::Midi)
			stopMidiVoice (slot, 0);
		else
			mDsp.setSlotPlaying (slot, false);
	}
}

//------------------------------------------------------------------------
void Project6Processor::clearFinishedOneShots (ProcessData& data)
{
	IParameterChanges* changes = data.outputParameterChanges;

	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		// Only a pad that is LAUNCHED and set to one-shot can finish. A
		// looping pad never does, and an armed pad waiting for its bar
		// line has not started.
		if (!mLaunched[slot] || mLoop[slot])
			continue;

		const bool sounding = (mKind[slot] == SlotFileKind::Midi)
			? mMidiVoices[slot].playing ()
			: mDsp.slotSounding (slot);

		if (sounding)
			continue;

		// IT HAS PLAYED. Everything the processor believes about this pad
		// goes back to stopped FIRST, so that nothing - a grid line later
		// in this block, the next block's arming - can see it half way
		// between the two states and start it again.
		mLaunched[slot] = false;
		mArmed[slot]    = false;
		mParams[slotPlayParam (slot)] = 0.0;

		// AND THE HOST IS TOLD, so the pad goes dark, a second click is
		// another hit rather than an "off", and a host recording
		// automation sees the pad turn itself off.
		//
		// NOT through publishOne, which only sends what MOVED and would
		// therefore stay silent the second time a pad finished at the
		// same value. This is an event, not a published state.
		if (changes != nullptr)
		{
			int32 index = 0;
			if (auto* queue = changes->addParameterData (slotPlayParam (slot), index))
			{
				int32 point = 0;
				queue->addPoint (0, 0.0, point);
			}
		}
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

	// THE TEMPO, for the pads' tooltips. Zero when the host has not said,
	// which is what makes the panel's fitSpeed() agree with the DSP's -
	// they are the same function given the same number.
	//
	// publishOne only sends what MOVED, so a fixed-tempo project pays for
	// this once and not once a block.
	publishOne (changes, kLiveTempo,
	            liveTempoDef ().toNormalized (
	                info.tempoKnown ? std::min (999.0, std::max (0.0, info.tempoBpm))
	                                : 0.0));

	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		// WHICH KIND OF PAD IS SOUNDING is asked of whichever half is
		// playing it. The panel does not care which - a lit pad is a lit
		// pad - which is the whole reason this is one published value
		// rather than two.
		const bool sounding = (mKind[slot] == SlotFileKind::Midi)
			? mMidiVoices[slot].playing ()
			: mDsp.slotSounding (slot);

		publishOne (changes, liveSlotParam (slot), sounding ? 1.0 : 0.0);
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Processor::terminate ()
{
	// Nothing is rendering by the time terminate is reached, so the
	// retire list can go without waiting for the block counter.
	closeMidiLog ();

	mActive.store (false, std::memory_order_release);
	for (int index = 0; index < kSlotCount; ++index)
	{
		mDsp.setSlotSample (index, nullptr);
		mLiveClips[index].store (nullptr, std::memory_order_release);
		mMidiVoices[index].reset ();
	}
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

	// A SECOND CHANCE TO NOTICE THE LOG FILE. setActive is the first, but
	// a host that was already running when the file appeared would not
	// call it again; this one it does call on every start. Both are the
	// UI thread.
	openMidiLog ();

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

		// The MIDI voices go back to silent WITHOUT EMITTING ANYTHING.
		// There is no block to put note-offs in on an activate, and the
		// host has not been sent anything to hang yet.
		for (MidiVoice& voice : mMidiVoices)
			voice.reset ();
		mMidiQueued = 0;

		// Same reason as in setupProcessing: reset() stopped every voice.
		for (bool& launched : mLaunched)
			launched = false;
		mBarClock.reset ();
		mWasPlaying = false;

		openMidiLog ();

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
			progress[slot] = (mKind[slot] == SlotFileKind::Midi)
				? mMidiVoices[slot].progress ()
				: mDsp.slotProgress (slot);

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
namespace {

/** A MIDI file's verdict, said in the language the panel already speaks.

    ONE STATUS PER SLOT, not two. A pad shows one tooltip and the person
    reading it does not care which of two enums the answer came out of -
    what they care about is why the pad will not play. SampleStatus grew
    two members rather than the panel growing a second code path. */
SampleStatus statusOfMidi (MidiStatus status)
{
	switch (status)
	{
		case MidiStatus::Loaded:            return SampleStatus::Loaded;
		case MidiStatus::NotMidi:           return SampleStatus::NotMidi;
		case MidiStatus::UnsupportedFormat: return SampleStatus::UnsupportedFormat;
		case MidiStatus::TooLong:           return SampleStatus::TooLong;
		case MidiStatus::NoNotes:           return SampleStatus::NoNotes;
	}
	return SampleStatus::Unreadable;
}

} // namespace

//------------------------------------------------------------------------
void Project6Processor::loadSlot (int index)
{
	if (!isSlotIndex (index))
		return;

	const std::string& path = mSlots.path (index);

	// WHICH READER, decided once and remembered. A pad holds either kind
	// of file and nothing below here tests an extension again.
	const SlotFileKind kind = slotFileKind (path);
	const SlotFileKind wasKind = mKind[index];
	mKind[index] = kind;

	// A PAD THAT CHANGED KIND WHILE IT WAS RUNNING - a .wav dropped on a
	// playing .mid pad, or the reverse. The two are played by different
	// machinery, so whichever was running has to be stopped and the pad
	// brought back in on the next grid line by the right one.
	//
	// It matters most in the MIDI direction: renderMidi skips a slot that
	// is not a MIDI slot, so a voice left playing there would never be
	// rendered again, never reach its own note-offs, and HANG. Replacing
	// the file is not one of the ways a note is allowed to be left on.
	//
	// A flag rather than the deed itself, because this is the UI thread:
	// stopping a voice means queueing note-offs, and note-offs need a
	// block to go in.
	if (wasKind != kind)
		mRelaunch[index].store (true, std::memory_order_release);

	SampleStatus status = SampleStatus::Empty;
	std::shared_ptr<const SampleBuffer> sample;
	std::shared_ptr<const MidiClip> clip;

	if (kind == SlotFileKind::Audio)
	{
		SampleBuffer buffer;
		status = loadWavFile (path, buffer);
		if (status == SampleStatus::Loaded)
			sample = std::make_shared<const SampleBuffer> (std::move (buffer));
	}
	else if (kind == SlotFileKind::Midi)
	{
		MidiClip parsed;
		const MidiStatus midi = loadMidiFile (path, parsed);
		status = statusOfMidi (midi);
		if (midi == MidiStatus::Loaded)
			clip = std::make_shared<const MidiClip> (std::move (parsed));
	}
	else if (!path.empty ())
	{
		// A path this plug-in does not take. It cannot normally get here
		// - the drop handler refuses it - but a project file written by
		// hand, or by a future build with a longer list, can.
		status = SampleStatus::UnsupportedFormat;
	}

	// A FAILED LOAD STILL KEEPS THE PATH. The slot goes on showing the
	// file's name and the project goes on remembering it - the file may
	// simply be on a drive that is not plugged in today. What it does not
	// get is audio or notes, and the status is how the panel says so.
	mStatus[index] = status;
	publishSlot (index, std::move (sample), std::move (clip));
	sendSlotStatusToController (index);
}

//------------------------------------------------------------------------
void Project6Processor::loadAllSlots ()
{
	for (int index = 0; index < kSlotCount; ++index)
		loadSlot (index);
}

//------------------------------------------------------------------------
void Project6Processor::publishSlot (int index, std::shared_ptr<const SampleBuffer> sample,
                                     std::shared_ptr<const MidiClip> clip)
{
	if (!isSlotIndex (index))
		return;

	std::shared_ptr<const SampleBuffer> previousSample = std::move (mSamples[index]);
	std::shared_ptr<const MidiClip>     previousClip   = std::move (mClips[index]);

	mSamples[index] = std::move (sample);
	mClips[index]   = std::move (clip);

	// The DSP gets a bare pointer into a buffer this object owns. It is
	// only ever read there, and never freed there.
	mDsp.setSlotSample (index, mSamples[index] ? mSamples[index].get () : nullptr);

	// RELEASE, exactly as the DSP's own store is, and for the same
	// reason: a thread that acquires this pointer must also see the notes
	// that were written before it was published.
	mLiveClips[index].store (mClips[index] ? mClips[index].get () : nullptr,
	                         std::memory_order_release);

	// The counter is read AFTER the swap, so any block still holding the
	// old pointer started at or before this value.
	const std::uint64_t at = mBlockCounter.load (std::memory_order_acquire);
	if (previousSample || previousClip)
		mRetired.push_back ({ std::move (previousSample), std::move (previousClip), at });

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

		// WHAT WAS READ OUT OF THE FILE, on the same message. Zero and
		// TempoSource::None for an empty slot or one whose file failed,
		// so a pad that will not play never shows a tempo.
		double tempo = 0.0;
		int64  source = static_cast<int64> (TempoSource::None);
		int64  oneShot = 0;
		if (const auto& sample = mSamples[index])
		{
			tempo   = sample->tempoBpm;
			source  = static_cast<int64> (sample->tempoSource);
			oneShot = sample->oneShot ? 1 : 0;
		}
		message->getAttributes ()->setFloat (kProject6SlotTempoAttribute, tempo);
		message->getAttributes ()->setInt (kProject6SlotTempoSrcAttribute, source);
		message->getAttributes ()->setInt (kProject6SlotOneShotAttribute, oneShot);

		// And the MIDI side of the same question.
		int64 notes = 0;
		double beats = 0.0;
		if (const auto& clip = mClips[index])
		{
			notes = clip->noteCount ();
			beats = clip->content;
		}
		message->getAttributes ()->setInt (kProject6SlotKindAttribute,
		                                   static_cast<int64> (mKind[index]));
		message->getAttributes ()->setInt (kProject6SlotNotesAttribute, notes);
		message->getAttributes ()->setFloat (kProject6SlotBeatsAttribute, beats);

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

		// And what this pad does about the project's tempo. Read every
		// block for the same reason: it is a conversion, not a state.
		mDsp.setSlotFitMode (slot, fitModeFromIndex (static_cast<int> (
			slotFitDef ().toInternal (mParams[slotFitParam (slot)]))));

		// And whether it loops or plays once. Read every block for the
		// same reason again - and read into BOTH halves, because a pad
		// holds either kind of file and the MIDI side needs it too.
		mLoop[slot] = mParams[slotLoopParam (slot)] >= 0.5;
		mDsp.setSlotLoop (slot, mLoop[slot]);

		// And how far this pad's MIDI is moved. Pushed every block, into
		// the voice rather than the DSP, because the voice is what turns
		// a clip into events - and the voice only ADOPTS it at the top
		// of a rendered block, after flushing whatever it has sounding,
		// so setting it sixty-four times a block is free and changing it
		// mid-note is impossible. See MidiVoice::setTranspose.
		mMidiVoices[slot].setTranspose (static_cast<int> (
			slotTransposeDef ().toInternal (mParams[slotTransposeParam (slot)])));
	}

	// And the eight row buses, the same way.
	for (int row = 0; row < kSlotRows; ++row)
		mDsp.setRowLevelDb (row, rowLevelDef ().toInternal (mParams[rowLevelParam (row)]));

	const TransportInfo transport = readTransport (data);

	// Nothing carried over from the last block. Everything a pad emits is
	// produced and handed over inside one call.
	mMidiQueued = 0;

	// A PAD WHOSE FILE CHANGED KIND since the last block. Stop whichever
	// machinery was playing it and unlaunch it, so the next grid line
	// brings it back in on the right one - see loadSlot.
	for (int slot = 0; slot < kSlotCount; ++slot)
	{
		if (!mRelaunch[slot].exchange (false, std::memory_order_acq_rel))
			continue;

		stopMidiVoice (slot, 0);
		mDsp.setSlotPlaying (slot, false);
		mLaunched[slot] = false;
	}

	// Quarter notes per sample, for placing a MIDI pad's launch and its
	// notes. Zero when the host has given us no musical context, which is
	// the one case in which a MIDI pad cannot play at all - see
	// quartersPerSample.
	const double midiPerSample = quartersPerSample (transport);

	// THE PROJECT'S TEMPO, for the pads that are fitted to it. Gated on
	// tempoKnown and not on `musical`: a stopped transport still has a
	// tempo, and a host that reports a tempo without a position can
	// still say what 90 BPM is. A host that reports NO tempo sends zero,
	// which fitSpeed turns into no fit at all rather than a guess.
	mDsp.setProjectTempo (transport.tempoKnown ? transport.tempoBpm : 0.0);

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
		applyGridLine (0, 0, transport.ppq, midiPerSample);
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
		applyGridLine (0, 0, transport.ppq, midiPerSample);
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
			applyGridLine (gridLines[lineIndex].step, at, transport.ppq, midiPerSample);
			++lineIndex;
		}
		else
		{
			handleEvent (event);
			++eventIndex;
		}
	}

	renderSegment (data, position, data.numSamples - position);

	//--------------------------------------------------------------------
	// THE MIDI, once for the whole block and after the audio.
	//
	// It does not need to be interleaved with the rendering the way a bar
	// line does, because an event carries its own sample offset: the host
	// places it, not us. What it does need is to happen after the grid
	// lines have been applied, so that a pad launched at sample 500 has
	// already been started before its notes are asked for.
	//--------------------------------------------------------------------
	renderMidi (transport, data.numSamples);
	flushMidi (data, transport.ppq, midiPerSample);

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

	clearFinishedOneShots (data);
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
	writeValueBlock (streamer, &mParams[kSlotFitBase], kSlotCount);
	writeValueBlock (streamer, &mParams[kSlotLoopBase], kSlotCount);
	writeValueBlock (streamer, &mParams[kSlotTransposeBase], kSlotCount);

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
	readValueBlock (streamer, &mParams[kSlotFitBase], kSlotCount,
	                slotFitDef ().defaultNormalized ());
	readValueBlock (streamer, &mParams[kSlotLoopBase], kSlotCount,
	                slotLoopDef ().defaultNormalized ());
	readValueBlock (streamer, &mParams[kSlotTransposeBase], kSlotCount,
	                slotTransposeDef ().defaultNormalized ());

	// The paths are back; now read the files. setState is not the audio
	// thread, so this is where sixty-four disk reads belong - and a slot
	// whose file has moved since the project was saved gets a status
	// rather than silence.
	loadAllSlots ();

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Project6
