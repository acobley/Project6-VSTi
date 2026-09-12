//------------------------------------------------------------------------
// Project6 - what a synth ACTUALLY receives, printed
//
//   c++ -std=c++17 -O2 -Wall -Isource tools/midi-trace.cpp
//       source/Project6Midi.cpp -o ~/p6obj/midi-trace
//   ~/p6obj/midi-trace loop.mid [--tempo 120] [--rate 44100]
//       [--block 512] [--cycle 4] [--bars 12]
//
//   (one line, wrapped; a comment line may not end in a backslash)
//
// This exists because a bug was reported that the tests could not see: a
// pattern that plays correctly into a drum machine and silences a
// sustaining synth at the transport's loop point. The tests assert what
// the voice SHOULD do from clips built by hand; this runs a REAL FILE
// through the real reader and the real voice, drives it with a looping
// transport, and prints every event with the sample it lands on.
//
// It is not a test - it asserts nothing and is not run by anything. It is
// for reading, and for the one question a unit test is bad at: given this
// file, exactly what comes out, and in what order?
//
// WHAT TO LOOK FOR, in the order they have actually bitten:
//
//   * an ON and an OFF of the same pitch on the same sample - the synth
//     then has to guess which came last, and some guess wrong for ever;
//   * a note that is still sounding when the trace ends, which on a synth
//     with a voice limit is silence a few passes later;
//   * an OFF with no ON before it, which some synths answer by cutting a
//     note another pad is playing;
//   * events out of sample order within a block, which a host is entitled
//     to reject wholesale.
//
// Every one of those is checked and counted at the bottom.
//------------------------------------------------------------------------

#include "Project6Midi.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Project6;

namespace {

const char* noteName (int pitch)
{
	static const char* const kNames[12] =
		{ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	static char buffer[16];
	std::snprintf (buffer, sizeof (buffer), "%s%d", kNames[pitch % 12], (pitch / 12) - 1);
	return buffer;
}

double argValue (int argc, char** argv, const char* name, double fallback)
{
	for (int i = 1; i + 1 < argc; ++i)
		if (std::strcmp (argv[i], name) == 0)
			return std::atof (argv[i + 1]);
	return fallback;
}

} // namespace

//------------------------------------------------------------------------
int main (int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf ("usage: midi-trace FILE.mid [--tempo 120] [--rate 44100] "
		             "[--block 512] [--cycle 4] [--bars 12]\n"
		             "  --cycle is the transport's loop length IN BARS; 0 means "
		             "no looping.\n");
		return 2;
	}

	const double tempo = argValue (argc, argv, "--tempo", 120.0);
	const double rate  = argValue (argc, argv, "--rate", 44100.0);
	const int    block = static_cast<int> (argValue (argc, argv, "--block", 512.0));
	const double cycleBars = argValue (argc, argv, "--cycle", 4.0);
	const double bars  = argValue (argc, argv, "--bars", 12.0);

	// WHERE THE PAD WAS LAUNCHED, in bars. This is the detail that makes
	// the difference in practice and that a test written from the inside
	// never thinks of: nobody starts the transport and the pad at the
	// same instant. You roll the transport, then click a pad, and it
	// comes in on the next bar line - so its launch point is usually
	// AFTER the start of the cycle, and every cycle wrap then lands
	// BEFORE it.
	const double launchBars = argValue (argc, argv, "--launch", 0.0);

	//--------------------------------------------------------------------
	// The file, through the reader the plug-in actually uses.
	//--------------------------------------------------------------------
	MidiClip clip;
	const MidiStatus status = loadMidiFile (argv[1], clip);

	static const char* const kStatus[] =
		{ "Loaded", "NotMidi", "UnsupportedFormat", "TooLong", "NoNotes" };
	std::printf ("file        %s\n", argv[1]);
	std::printf ("status      %s\n", kStatus[static_cast<int> (status)]);
	if (status != MidiStatus::Loaded)
		return 1;

	const double bar  = barQuartersFor (4, 4);
	const double loop = loopLengthQuarters (clip.content, bar);

	std::printf ("notes       %d\n", clip.noteCount ());
	std::printf ("content     %.4f quarter notes (%.4f bars)\n", clip.content, clip.content / bar);
	std::printf ("loops as    %.4f quarter notes (%.2f bars)\n", loop, loop / bar);
	std::printf ("file says   %d/%d", clip.sigNumerator, clip.sigDenominator);
	if (clip.fileTempoBpm > 0.0)
		std::printf (", %.2f BPM (ignored - notes are in quarter notes)", clip.fileTempoBpm);
	std::printf ("\n\n");

	// The notes, so a suspicious one can be found by eye. Anything that
	// ENDS AT OR AFTER THE LOOP is the kind a drum machine never has and
	// a pad part always does.
	std::printf ("  %-5s %-7s %-9s %-9s %s\n", "#", "pitch", "start", "end", "");
	for (int i = 0; i < clip.noteCount () && i < 64; ++i)
	{
		const MidiNote& n = clip.notes[static_cast<std::size_t> (i)];
		const bool crosses = (n.end >= loop - 1e-9);
		std::printf ("  %-5d %-7s %-9.4f %-9.4f %s\n", i, noteName (n.note),
		             n.start, n.end, crosses ? "<-- held to the loop end" : "");
	}
	if (clip.noteCount () > 64)
		std::printf ("  ... and %d more\n", clip.noteCount () - 64);
	std::printf ("\n");

	//--------------------------------------------------------------------
	// Now play it, the way the processor does.
	//--------------------------------------------------------------------
	const double perSample = tempo / (60.0 * rate);
	const double cycle = cycleBars * bar;

	std::printf ("playing     %.0f BPM, %.0f Hz, %d-sample blocks, cycle %.0f bars, "
	             "pad launched at bar %.0f\n",
	             tempo, rate, block, cycleBars, launchBars + 1.0);
	std::printf ("            (a cycle of 0 bars means the transport does not loop)\n\n");

	MidiVoice voice;
	voice.start (launchBars * bar);

	MidiEventOut out[kMaxMidiEventsPerBlock];

	int  sounding[128] = {};
	int  collisions = 0, orphans = 0, disordered = 0;
	long totalOn = 0, totalOff = 0;

	// COUNTED IN BLOCKS, not in project time: with a cycle, project time
	// goes round and round and never reaches the end.
	const double totalQuarters = bars * bar;
	const long totalBlocks = static_cast<long> (totalQuarters / (perSample * block)) + 1;
	long blocksDone = 0;
	double ppq = 0.0;
	int blockIndex = 0;
	int stuckSeen = 0;

	while (blocksDone++ < totalBlocks)
	{
		const double blockQuarters = perSample * block;

		// THE CYCLE. The host plays to the end of it and jumps back, which
		// is the moment the bug was heard at.
		bool wrapped = false;
		if (cycle > 0.0 && ppq + blockQuarters > cycle)
		{
			// Print the boundary so the events either side of it are easy
			// to find in the output.
			std::printf ("  ---- transport loops back to 0 ----\n");
			ppq = 0.0;
			wrapped = true;
		}

		const int count = voice.render (&clip, loop, ppq, perSample, block, true, out,
		                                kMaxMidiEventsPerBlock);

		int lastOffset = -1;
		int offAt[128];
		for (int i = 0; i < 128; ++i)
			offAt[i] = -1;

		for (int i = 0; i < count; ++i)
		{
			const MidiEventOut& e = out[i];

			if (e.sampleOffset < lastOffset)
				++disordered;
			lastOffset = e.sampleOffset;

			const char* flag = "";
			if (e.noteOn)
			{
				++totalOn;
				if (offAt[e.note] >= 0 && e.sampleOffset <= offAt[e.note])
				{
					++collisions;
					flag = "  <<< ON ON TOP OF ITS OWN OFF";
				}
				++sounding[e.note];
			}
			else
			{
				++totalOff;
				offAt[e.note] = e.sampleOffset;
				if (sounding[e.note] <= 0)
				{
					++orphans;
					flag = "  <<< OFF WITH NO ON";
				}
				else
				{
					--sounding[e.note];
				}
			}

			std::printf ("  blk %-5d ppq %9.4f  +%-5d  %-4s %-5s v%-4d%s\n",
			             blockIndex, ppq, e.sampleOffset,
			             e.noteOn ? "ON" : "off", noteName (e.note),
			             e.velocity, flag);
		}

		if (wrapped && count == 0)
			std::printf ("  blk %-5d ppq %9.4f  (nothing)\n", blockIndex, ppq);

		// HOW MANY NOTES ARE HELD RIGHT NOW. A pattern of sixteenth notes
		// should never hold many for long; a count that climbs and does
		// not come down is notes being stranded, which is silence on a
		// synth with a voice limit and invisible on a drum machine.
		int held = 0;
		for (int note = 0; note < 128; ++note)
			held += sounding[note];
		if (held > stuckSeen)
		{
			stuckSeen = held;
			std::printf ("      .... %d notes held at once (ppq %.3f)\n", held, ppq);
		}

		ppq += blockQuarters;
		++blockIndex;
	}

	//--------------------------------------------------------------------
	int stuck = 0;
	for (int note = 0; note < 128; ++note)
		if (sounding[note] > 0)
		{
			std::printf ("\n  STILL SOUNDING at the end: %s x%d", noteName (note),
			             sounding[note]);
			stuck += sounding[note];
		}

	std::printf ("\n\n%ld note-ons, %ld note-offs\n", totalOn, totalOff);
	std::printf ("%d on-top-of-its-own-off, %d orphan offs, %d out of order, "
	             "%d left sounding\n",
	             collisions, orphans, disordered, stuck);
	std::printf ("%s\n", (collisions == 0 && orphans == 0 && disordered == 0 && stuck == 0)
	                         ? "NOTHING WRONG WITH WHAT THIS PLUG-IN SENDS."
	                         : "^^ something above is worth explaining.");
	return 0;
}
