//------------------------------------------------------------------------
// Project6 - MIDI file and MIDI voice tests
//
// SDK-FREE, and disk-free: every file here is built BYTE BY BYTE in
// memory and handed to parseMidiFile, which is the whole reason the parse
// is split from the file read. Running status, a zero-velocity note-off, a
// SysEx dump in the middle of a track and a truncated chunk are all things
// a real file does and none of them is something you can cover by finding
// a .mid on disk and hoping.
//
//     c++ -std=c++17 -O2 -Wall -Isource tests/MidiTests.cpp
//         source/Project6Midi.cpp -o ~/p6obj/miditests && ~/p6obj/miditests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//
// The second half is the VOICE, and its assertions are all versions of one
// question: IS A NOTE LEFT ON? That is the failure mode of every MIDI
// looper, it is silent until somebody notices a drone, and it is the only
// thing in this file that cannot be fixed after the fact by the user.
//------------------------------------------------------------------------

#include "Project6Midi.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Project6;

namespace {

int gFailures = 0;

void check (bool condition, const char* what)
{
	std::printf ("  %-64s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++gFailures;
}

void section (const char* title)
{
	std::printf ("\n%s\n", title);
}

bool close (double a, double b, double tolerance)
{
	return std::fabs (a - b) <= tolerance;
}

//------------------------------------------------------------------------
// A standard MIDI file builder. Deliberately NOT sharing code with the
// parser - a test that used the reader's own idea of the layout would
// agree with it about a mistake. BIG-ENDIAN, which is the opposite of the
// wav builder next door and the first thing to get wrong.
//------------------------------------------------------------------------
struct Bytes
{
	std::vector<unsigned char> data;

	void u8 (int v)  { data.push_back (static_cast<unsigned char> (v & 0xFF)); }
	void u16 (int v) { u8 (v >> 8); u8 (v); }
	void u32 (uint32_t v)
	{
		u8 (static_cast<int> (v >> 24)); u8 (static_cast<int> (v >> 16));
		u8 (static_cast<int> (v >> 8));  u8 (static_cast<int> (v));
	}
	void tag (const char* t) { for (int i = 0; i < 4; ++i) u8 (t[i]); }
	void raw (const std::vector<unsigned char>& b) { data.insert (data.end (), b.begin (), b.end ()); }

	/** A variable-length quantity, seven bits a byte, high bit set on all
	    but the last. Written the long way round on purpose. */
	void varLen (uint32_t v)
	{
		unsigned char stack[5];
		int count = 0;
		stack[count++] = static_cast<unsigned char> (v & 0x7Fu);
		v >>= 7;
		while (v > 0)
		{
			stack[count++] = static_cast<unsigned char> ((v & 0x7Fu) | 0x80u);
			v >>= 7;
		}
		while (count > 0)
			u8 (stack[--count]);
	}
};

/** MThd, then the tracks. `division` is ticks per quarter note when
    positive; a negative one is the SMPTE form this reader refuses. */
std::vector<unsigned char> buildMidi (int format, int division,
                                      const std::vector<Bytes>& tracks,
                                      int claimedTrackCount = -1,
                                      int truncateLastBy = 0)
{
	Bytes file;
	file.tag ("MThd");
	file.u32 (6);
	file.u16 (format);
	file.u16 (claimedTrackCount >= 0 ? claimedTrackCount
	                                 : static_cast<int> (tracks.size ()));
	file.u16 (division);

	for (std::size_t t = 0; t < tracks.size (); ++t)
	{
		const bool last = (t + 1 == tracks.size ());
		file.tag ("MTrk");
		file.u32 (static_cast<uint32_t> (tracks[t].data.size ()));

		if (last && truncateLastBy > 0)
		{
			// The chunk CLAIMS its full length and the file stops short.
			const std::size_t keep = tracks[t].data.size ()
			                         - static_cast<std::size_t> (truncateLastBy);
			for (std::size_t i = 0; i < keep; ++i)
				file.u8 (tracks[t].data[i]);
		}
		else
		{
			file.raw (tracks[t].data);
		}
	}

	return file.data;
}

/** delta, note on. */
void noteOn (Bytes& t, uint32_t delta, int channel, int note, int velocity)
{
	t.varLen (delta);
	t.u8 (0x90 | (channel & 0x0F));
	t.u8 (note);
	t.u8 (velocity);
}

/** delta, note off. */
void noteOff (Bytes& t, uint32_t delta, int channel, int note)
{
	t.varLen (delta);
	t.u8 (0x80 | (channel & 0x0F));
	t.u8 (note);
	t.u8 (64);
}

void endOfTrack (Bytes& t, uint32_t delta = 0)
{
	t.varLen (delta);
	t.u8 (0xFF); t.u8 (0x2F); t.u8 (0x00);
}

constexpr int kDivision = 480;      // ticks per quarter note

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - MIDI file and MIDI voice tests\n");

	//--------------------------------------------------------------------
	section ("1. One note, which is the whole format in miniature");
	//--------------------------------------------------------------------
	{
		Bytes track;
		noteOn  (track, 0, 0, 60, 100);
		noteOff (track, kDivision, 0, 60);
		endOfTrack (track);

		MidiClip clip;
		check (parseMidiFile (buildMidi (0, kDivision, { track }).data (),
		                      buildMidi (0, kDivision, { track }).size (), clip)
		           == MidiStatus::Loaded, "a format 0 file with one note loads");
		check (clip.noteCount () == 1,               "one note");
		check (clip.notes[0].note == 60,             "middle C");
		check (clip.notes[0].velocity == 100,        "at the velocity the file said");

		// IN QUARTER NOTES, not ticks and not seconds. 480 ticks at 480
		// ticks per quarter is exactly one quarter note.
		check (close (clip.notes[0].start, 0.0, 1e-12), "starting at zero");
		check (close (clip.notes[0].end,   1.0, 1e-12), "and one QUARTER NOTE long");
		check (close (clip.content,        1.0, 1e-12), "and the clip is that long");

		// THE BIG-ENDIAN CHECK, which is the bug that does not announce
		// itself: a division of 480 read little-endian is 61440, and every
		// note would land in the first two hundredths of a beat.
		check (! close (clip.notes[0].end, 1.0 * 480.0 / 61440.0, 1e-6),
		       "NEGATIVE CONTROL: the division was not read little-endian");
	}

	//--------------------------------------------------------------------
	section ("2. The things real files do that trip a reader");
	//--------------------------------------------------------------------
	{
		// RUNNING STATUS: the status byte omitted, the previous one still
		// in force. Hardware sequencers write whole passages this way and
		// a reader without it reads one note and then garbage.
		{
			Bytes track;
			noteOn (track, 0, 0, 60, 100);
			track.varLen (kDivision);          // no status byte
			track.u8 (62); track.u8 (90);      // ...another note on, running
			noteOff (track, kDivision, 0, 60);
			noteOff (track, 0, 0, 62);
			endOfTrack (track);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::Loaded,
			       "running status loads");
			check (clip.noteCount () == 2, "and both notes are found");
			check (clip.notes[1].note == 62, "the second is the one with no status byte");
			check (close (clip.notes[1].start, 1.0, 1e-12), "a quarter in");
		}

		// A NOTE ON WITH VELOCITY ZERO IS A NOTE OFF. Most files prefer it,
		// because running status then covers a whole passage - and a reader
		// that takes it at face value leaves every note on for ever.
		{
			Bytes track;
			noteOn (track, 0, 0, 60, 100);
			noteOn (track, kDivision, 0, 60, 0);       // this is an OFF
			endOfTrack (track);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::Loaded,
			       "a zero-velocity note-on loads");
			check (clip.noteCount () == 1, "as ONE note");
			check (close (clip.notes[0].end, 1.0, 1e-12),
			       "which ends where the zero-velocity note-on was");
			check (! close (clip.notes[0].end, clip.notes[0].start, 1e-9),
			       "NEGATIVE CONTROL: not a second note starting there");
		}

		// SYSEX, skipped by its own declared length. Missing this is the
		// other classic: a dump of a few hundred bytes read as events
		// turns the rest of the track into noise.
		{
			Bytes track;
			track.varLen (0);
			track.u8 (0xF0);
			track.varLen (5);
			for (int i = 0; i < 4; ++i) track.u8 (0x7D);
			track.u8 (0xF7);
			noteOn  (track, 0, 0, 64, 80);
			noteOff (track, kDivision, 0, 64);
			endOfTrack (track);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::Loaded,
			       "a track with a SysEx dump in it loads");
			check (clip.noteCount () == 1, "and the note AFTER the dump is still found");
			check (clip.notes[0].note == 64, "as itself");
		}

		// Meta events: the time signature is kept to be shown, the tempo
		// is kept to be shown and USED FOR NOTHING, and anything else is
		// skipped by its own length.
		{
			Bytes track;

			// A text meta first, which must be stepped over by its length
			// and not read as events.
			track.varLen (0);
			track.u8 (0xFF); track.u8 (0x01); track.varLen (4);
			track.u8 ('l'); track.u8 ('o'); track.u8 ('o'); track.u8 ('p');

			// 6/8: numerator 6, denominator as a power of two, so 3.
			track.varLen (0);
			track.u8 (0xFF); track.u8 (0x58); track.varLen (4);
			track.u8 (6); track.u8 (3); track.u8 (24); track.u8 (8);

			// 500000 microseconds per quarter note is 120 BPM.
			track.varLen (0);
			track.u8 (0xFF); track.u8 (0x51); track.varLen (3);
			track.u8 (0x07); track.u8 (0xA1); track.u8 (0x20);

			noteOn  (track, 0, 0, 67, 70);
			noteOff (track, kDivision, 0, 67);
			endOfTrack (track);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::Loaded,
			       "a track full of meta events loads");
			check (clip.noteCount () == 1,      "and its one note survives them");
			check (clip.sigNumerator == 6,      "the time signature's numerator is read");
			check (clip.sigDenominator == 8,    "and its denominator is the power of two");
			check (close (clip.fileTempoBpm, 120.0, 1e-6), "and the tempo is read");

			// The file's tempo changes NOTHING about the notes: they are
			// in quarter notes, and a quarter note is a quarter note at
			// any tempo. This is what makes MIDI pads need no tempo
			// fitting at all - see the header.
			check (close (clip.notes[0].end, 1.0, 1e-12),
			       "NEGATIVE CONTROL: the tempo did not scale the note");
		}

		// OVERLAPPING STRIKES OF ONE PITCH. A note-off pairs with the
		// OLDEST matching note-on; keeping one open note per pitch would
		// silently merge these two into one long one.
		{
			Bytes track;
			noteOn  (track, 0, 0, 60, 100);
			noteOn  (track, kDivision, 0, 60, 100);        // struck again, still held
			noteOff (track, kDivision, 0, 60);             // releases the FIRST
			noteOff (track, kDivision, 0, 60);             // releases the second
			endOfTrack (track);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			parseMidiFile (bytes.data (), bytes.size (), clip);
			check (clip.noteCount () == 2, "one pitch struck twice is two notes");
			check (close (clip.notes[0].start, 0.0, 1e-12) && close (clip.notes[0].end, 2.0, 1e-12),
			       "the first runs from 0 to 2");
			check (close (clip.notes[1].start, 1.0, 1e-12) && close (clip.notes[1].end, 3.0, 1e-12),
			       "and the second from 1 to 3");
		}

		// A NOTE STILL HELD WHEN THE FILE ENDS is ended there rather than
		// left open - an open note is a note the player would hold until
		// something else stopped the pad.
		{
			Bytes track;
			noteOn (track, 0, 0, 60, 100);
			track.varLen (kDivision * 2);
			track.u8 (0xFF); track.u8 (0x2F); track.u8 (0x00);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::Loaded,
			       "a file whose note is never released still loads");
			check (clip.noteCount () == 1, "with the note in it");
			check (close (clip.notes[0].end, 2.0, 1e-12),
			       "ended at the end of the track");
			check (clip.notes[0].end > clip.notes[0].start,
			       "NEGATIVE CONTROL: and not left with no length");
		}

		// A ZERO-LENGTH NOTE is dropped rather than emitted. An on and an
		// off at the same instant is swallowed by some instruments and
		// hung on by others, and it is never what was meant.
		{
			Bytes track;
			noteOn  (track, 0, 0, 60, 100);
			noteOff (track, 0, 0, 60);
			noteOn  (track, kDivision, 0, 62, 100);
			noteOff (track, kDivision, 0, 62);
			endOfTrack (track);

			const auto bytes = buildMidi (0, kDivision, { track });
			MidiClip clip;
			parseMidiFile (bytes.data (), bytes.size (), clip);
			check (clip.noteCount () == 1, "the zero-length note is dropped");
			check (clip.notes[0].note == 62, "and the real one is kept");
		}
	}

	//--------------------------------------------------------------------
	section ("3. Several tracks, merged onto one timeline");
	//--------------------------------------------------------------------
	{
		// A FORMAT 1 FILE IS ONE SONG spread over tracks, and every
		// track's delta times start again from zero. Walking each from
		// its own zero and merging is the whole of it.
		Bytes bass;
		noteOn  (bass, 0, 0, 36, 110);
		noteOff (bass, kDivision * 2, 0, 36);
		endOfTrack (bass);

		Bytes lead;
		noteOn  (lead, kDivision, 0, 72, 60);          // half way through the bass note
		noteOff (lead, kDivision, 0, 72);
		endOfTrack (lead);

		const auto bytes = buildMidi (1, kDivision, { bass, lead });
		MidiClip clip;
		check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::Loaded,
		       "a format 1 file with two tracks loads");
		check (clip.noteCount () == 2, "with both tracks' notes");

		// SORTED BY START, which after a merge they are not naturally -
		// and the player walks them in order and relies on it.
		check (clip.notes[0].note == 36 && clip.notes[1].note == 72,
		       "sorted into one timeline by start");
		check (close (clip.notes[1].start, 1.0, 1e-12),
		       "the second track's delta times ran from ITS zero, not the first's");
		check (close (clip.content, 2.0, 1e-12), "and the clip is as long as the longest");
	}

	//--------------------------------------------------------------------
	section ("4. Files that are refused, and why each one is");
	//--------------------------------------------------------------------
	{
		MidiClip clip;

		check (parseMidiFile (nullptr, 0, clip) == MidiStatus::NotMidi, "no bytes at all");

		const unsigned char rubbish[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E', 0, 0 };
		check (parseMidiFile (rubbish, sizeof (rubbish), clip) == MidiStatus::NotMidi,
		       "a wav file dropped on a MIDI slot is not a MIDI file");

		Bytes track;
		noteOn  (track, 0, 0, 60, 100);
		noteOff (track, kDivision, 0, 60);
		endOfTrack (track);

		// FORMAT 2 is a set of INDEPENDENT sequences, not one song.
		// Merging them would be inventing a piece of music.
		{
			const auto bytes = buildMidi (2, kDivision, { track });
			check (parseMidiFile (bytes.data (), bytes.size (), clip)
			           == MidiStatus::UnsupportedFormat, "format 2 is refused");
		}

		// A NEGATIVE DIVISION IS SMPTE: the file is timed in frames and
		// seconds, with no musical grid to loop to a bar of.
		{
			const auto bytes = buildMidi (0, 0xE728, { track });      // -25 fps, 40 ticks
			check (parseMidiFile (bytes.data (), bytes.size (), clip)
			           == MidiStatus::UnsupportedFormat, "an SMPTE-timed file is refused");
		}

		// A file with nothing to play says so, rather than loading as a
		// pad that lights up and does nothing.
		{
			Bytes empty;
			endOfTrack (empty);
			const auto bytes = buildMidi (0, kDivision, { empty });
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::NoNotes,
			       "a file with no notes in it is refused");
		}

		// Longer than the cap. 300 quarter notes is past kMaxMidiQuarters.
		{
			Bytes track2;
			noteOn  (track2, 0, 0, 60, 100);
			noteOff (track2, static_cast<uint32_t> (kDivision * 300), 0, 60);
			endOfTrack (track2);
			const auto bytes = buildMidi (0, kDivision, { track2 });
			check (parseMidiFile (bytes.data (), bytes.size (), clip) == MidiStatus::TooLong,
			       "a file longer than the cap is refused rather than truncated");
		}

		// A TRUNCATED TRACK still gives up what was actually there, the
		// same rule the wav reader follows for a cut-short data chunk.
		{
			Bytes track3;
			noteOn  (track3, 0, 0, 60, 100);
			noteOff (track3, kDivision, 0, 60);
			noteOn  (track3, 0, 0, 62, 100);
			noteOff (track3, kDivision, 0, 62);
			endOfTrack (track3);

			const auto bytes = buildMidi (0, kDivision, { track3 }, -1, 6);
			MidiClip cut;
			check (parseMidiFile (bytes.data (), bytes.size (), cut) == MidiStatus::Loaded,
			       "a truncated track loads what survived");
			check (cut.noteCount () >= 1, "which is at least the first note");
		}

		// A refused file leaves an EMPTY clip behind, not half of one.
		{
			const auto bytes = buildMidi (2, kDivision, { track });
			MidiClip leftovers;
			leftovers.notes.resize (5);
			parseMidiFile (bytes.data (), bytes.size (), leftovers);
			check (leftovers.empty (), "a refused file leaves an empty clip behind");
		}
	}

	//--------------------------------------------------------------------
	section ("5. Running the loop out to the end of the bar");
	//--------------------------------------------------------------------
	{
		// The bar length, which is the shared function.
		check (close (barQuartersFor (4, 4), 4.0, 1e-12), "4/4 is four quarter notes");
		check (close (barQuartersFor (3, 4), 3.0, 1e-12), "3/4 is three");
		check (close (barQuartersFor (6, 8), 3.0, 1e-12), "6/8 is three, not six");
		check (close (barQuartersFor (7, 8), 3.5, 1e-12), "and 7/8 is three and a half");
		check (close (barQuartersFor (0, 0), 4.0, 1e-12), "nonsense falls back to 4/4");

		// THE REQUEST THIS FEATURE EXISTS FOR. A file that stops half way
		// through its last bar runs on to the end of it.
		check (close (loopLengthQuarters (3.5, 4.0), 4.0, 1e-12),
		       "three and a half beats loops as a whole bar");
		check (close (loopLengthQuarters (8.5, 4.0), 12.0, 1e-12),
		       "and two bars and a bit loops as three");

		// A file that is ALREADY a whole number of bars is left alone.
		check (close (loopLengthQuarters (4.0, 4.0), 4.0, 1e-12), "an exact bar is not padded");
		check (close (loopLengthQuarters (16.0, 4.0), 16.0, 1e-12), "nor are four of them");

		// THE TOLERANCE, without which floating point pads a four-bar
		// loop to five and puts every loop in the bank a bar out.
		check (close (loopLengthQuarters (16.0 - 1e-9, 4.0), 16.0, 1e-12),
		       "and neither is one that came out a billionth of a beat short");
		check (! close (loopLengthQuarters (16.0 - 1e-9, 4.0), 20.0, 1e-12),
		       "NEGATIVE CONTROL: it is not rounded up to five bars");

		// It is the PROJECT's bar, so the same clip loops differently in
		// 3/4 - which is the reason this is applied at playback and not
		// baked into the clip when the file was read.
		check (close (loopLengthQuarters (3.5, 3.0), 6.0, 1e-12),
		       "the same clip in 3/4 runs out to two bars");

		// A clip with nothing in it is still one bar, not zero: a loop of
		// zero length would repeat infinitely often inside one block.
		check (close (loopLengthQuarters (0.0, 4.0), 4.0, 1e-12), "an empty clip is one bar");
		check (loopLengthQuarters (3.5, 0.0) > 0.0, "and a nonsense bar length is survivable");
	}

	//--------------------------------------------------------------------
	section ("6. Which channel a row plays out on");
	//--------------------------------------------------------------------
	{
		// A ROW IS ITS CHANNEL, which is what makes the merged MIDI
		// output usable as eight parts in a host that shows only one.
		check (midiChannelForRow (0) == 0, "row A is VST3 channel 0");
		check (midiChannelForRow (7) == 7, "and row H is VST3 channel 7");

		// TWO FUNCTIONS FOR ONE FACT, because VST3 counts channels from
		// zero and people count them from one. The panel prints the
		// second and the processor stamps the first, and the second is
		// DERIVED from the first - which is what stops the label saying
		// "channel 3" while the events go out on 4.
		check (midiChannelNumberForRow (0) == 1, "which a person calls channel 1");
		check (midiChannelNumberForRow (7) == 8, "and channel 8");

		bool derived = true;
		for (int row = 0; row < 8; ++row)
			derived &= (midiChannelNumberForRow (row) == midiChannelForRow (row) + 1);
		check (derived, "the printed number is always one past the stamped one");

		check (midiChannelNumberForRow (0) != midiChannelForRow (0),
		       "NEGATIVE CONTROL: and the two are not the same number");
	}

	//--------------------------------------------------------------------
	section ("7. The voice: what comes out, and when");
	//--------------------------------------------------------------------
	{
		// A clip built by hand rather than parsed - the parse is section
		// 1's job and this is about playback.
		MidiClip clip;
		clip.notes.push_back ({ 0.0, 1.0, 60, 100 });
		clip.notes.push_back ({ 2.0, 3.0, 64, 90 });
		clip.content = 3.5;

		// One quarter note per thousand samples, so the arithmetic is
		// done in the head rather than in the test.
		const double perSample = 1.0 / 1000.0;
		const double loop = loopLengthQuarters (clip.content, 4.0);
		check (close (loop, 4.0, 1e-12), "the clip loops as one bar of 4/4");

		MidiEventOut out[kMaxMidiEventsPerBlock];

		MidiVoice voice;
		check (! voice.playing (), "a new voice is not playing");
		check (voice.render (&clip, loop, 0.0, perSample, 1000, out, kMaxMidiEventsPerBlock) == 0,
		       "and emits nothing");

		// Launched at project position 0.
		voice.start (0.0);
		check (voice.playing (), "starting it starts it");

		int count = voice.render (&clip, loop, 0.0, perSample, 1000, out,
		                          kMaxMidiEventsPerBlock);
		check (count == 1, "the first beat is one note on");
		check (out[0].noteOn && out[0].note == 60 && out[0].sampleOffset == 0,
		       "at sample 0");
		check (out[0].velocity == 100, "at the file's velocity");

		// ITS OFF IS AT THE TOP OF THE NEXT BLOCK, not one sample before
		// the end of this one. Both edges are half-open, so an event
		// falling on a block boundary belongs to the block that starts
		// there - the same rule for ons and offs.
		count = voice.render (&clip, loop, 1.0, perSample, 1000, out, kMaxMidiEventsPerBlock);
		check (count == 1 && ! out[0].noteOn && out[0].note == 60,
		       "the second beat is that note's off");
		check (out[0].sampleOffset == 0, "at the top of the block, not the end of the last");

		count = voice.render (&clip, loop, 2.0, perSample, 1000, out, kMaxMidiEventsPerBlock);
		check (count == 1 && out[0].noteOn && out[0].note == 64,
		       "the third beat starts the second note");

		count = voice.render (&clip, loop, 3.0, perSample, 1000, out, kMaxMidiEventsPerBlock);
		check (count == 1 && ! out[0].noteOn && out[0].note == 64,
		       "and the fourth ends it");

		// AND THEN IT REPEATS, on the bar rather than at 3.5 where the
		// file stopped. This is the padding, heard rather than measured.
		// AND THEN IT REPEATS, on the bar line rather than at 3.5 where
		// the file stopped - which is the padding, heard rather than
		// measured. The fifth beat is the first beat again.
		count = voice.render (&clip, loop, 4.0, perSample, 1000, out, kMaxMidiEventsPerBlock);
		check (count == 1 && out[0].noteOn && out[0].note == 60 && out[0].sampleOffset == 0,
		       "the loop comes round again on the bar line");

		// NEGATIVE CONTROL for the padding itself: had the loop been the
		// file's own 3.5 beats, this repeat would have landed at 3.5 -
		// half way through the fourth beat - and not here.
		check (! close (loop, clip.content, 1e-9),
		       "NEGATIVE CONTROL: the loop is longer than the file");

		// The playhead the pad draws.
		check (voice.progress () > 0.f && voice.progress () <= 1.f,
		       "and the progress bar is somewhere inside the loop");
	}

	//--------------------------------------------------------------------
	section ("8. The voice: IS A NOTE LEFT ON?");
	//--------------------------------------------------------------------
	{
		MidiEventOut out[kMaxMidiEventsPerBlock];
		const double perSample = 1.0 / 1000.0;

		// A NOTE THAT RUNS PAST THE LOOP END is cut off at the boundary,
		// so the loop is self-contained and can repeat for ever without
		// notes piling up on every pass.
		{
			MidiClip clip;
			clip.notes.push_back ({ 3.5, 6.0, 60, 100 });     // runs past the bar
			clip.content = 3.5;

			const double loop = loopLengthQuarters (clip.content, 4.0);

			MidiVoice voice;
			voice.start (0.0);

			// Beats 0 to 3: the note starts half way through the fourth.
			voice.render (&clip, loop, 0.0, perSample, 3000, out, kMaxMidiEventsPerBlock);
			int count = voice.render (&clip, loop, 3.0, perSample, 1000, out,
			                          kMaxMidiEventsPerBlock);

			check (count == 2, "the note starts and is stopped inside the same beat");
			check (out[0].noteOn && out[0].sampleOffset == 500, "on half way through");
			check (! out[1].noteOn && out[1].sampleOffset == 999,
			       "and OFF at the loop end, not at 6.0 where the file put it");

			// The proof that nothing is left standing: a stop now has
			// nothing to send.
			check (voice.allNotesOff (0, out, kMaxMidiEventsPerBlock) == 0,
			       "and nothing at all is left sounding");
		}

		// STOPPED MID-NOTE. Every note this voice started is turned off,
		// wherever the pad was when it was stopped.
		{
			MidiClip clip;
			clip.notes.push_back ({ 0.0, 4.0, 60, 100 });
			clip.notes.push_back ({ 0.0, 4.0, 67, 100 });
			clip.content = 4.0;

			MidiVoice voice;
			voice.start (0.0);
			voice.render (&clip, 4.0, 0.0, perSample, 1000, out, kMaxMidiEventsPerBlock);

			const int count = voice.allNotesOff (37, out, kMaxMidiEventsPerBlock);
			check (count == 2, "a pad stopped mid-note turns both notes off");
			check (! out[0].noteOn && ! out[1].noteOn, "as note-offs");
			check (out[0].sampleOffset == 37 && out[1].sampleOffset == 37,
			       "at the sample it was stopped on");
			check (! voice.playing (), "and it is no longer playing");
			check (voice.allNotesOff (0, out, kMaxMidiEventsPerBlock) == 0,
			       "and stopping it twice sends nothing the second time");
		}

		// ONE OFF PER OUTSTANDING ON. A pitch struck twice and released
		// once is still sounding once.
		{
			MidiClip clip;
			clip.notes.push_back ({ 0.0, 4.0, 60, 100 });
			clip.notes.push_back ({ 0.5, 4.0, 60, 100 });      // the same pitch again
			clip.content = 4.0;

			MidiVoice voice;
			voice.start (0.0);
			voice.render (&clip, 4.0, 0.0, perSample, 1000, out, kMaxMidiEventsPerBlock);

			check (voice.allNotesOff (0, out, kMaxMidiEventsPerBlock) == 2,
			       "one pitch struck twice needs two note-offs");
		}

		// THE FILE TAKEN AWAY while the pad was running - the slot was
		// emptied, or the new file failed to load. Stop, and stop with
		// the note-offs rather than dead.
		{
			MidiClip clip;
			clip.notes.push_back ({ 0.0, 4.0, 60, 100 });
			clip.content = 4.0;

			MidiVoice voice;
			voice.start (0.0);
			voice.render (&clip, 4.0, 0.0, perSample, 1000, out, kMaxMidiEventsPerBlock);

			const int count = voice.render (nullptr, 4.0, 1.0, perSample, 1000, out,
			                                kMaxMidiEventsPerBlock);
			check (count == 1 && ! out[0].noteOn,
			       "a slot emptied under a running pad turns its note off");
			check (! voice.playing (), "and the pad stops");
		}

		// A HOST THAT JUMPED. The playhead was dragged somewhere else and
		// this block does not continue the last one.
		{
			MidiClip clip;
			clip.notes.push_back ({ 0.0, 4.0, 60, 100 });
			clip.content = 4.0;

			MidiVoice voice;
			voice.start (0.0);
			voice.render (&clip, 4.0, 0.0, perSample, 1000, out, kMaxMidiEventsPerBlock);

			const int count = voice.render (&clip, 4.0, 64.0, perSample, 1000, out,
			                                kMaxMidiEventsPerBlock);
			check (count >= 1 && ! out[0].noteOn && out[0].sampleOffset == 0,
			       "a locate turns off what was sounding before it");
		}

		// NO ORPHAN NOTE-OFFS. A voice that never sent an on must never
		// send the matching off - some instruments answer one by cutting
		// off a note another pad is playing.
		{
			MidiClip clip;
			clip.notes.push_back ({ 0.0, 2.0, 60, 100 });
			clip.content = 4.0;

			MidiVoice voice;
			voice.start (0.0);

			// Straight to the third beat: the note's ON was never sent.
			const int count = voice.render (&clip, 4.0, 1.5, perSample, 1000, out,
			                                kMaxMidiEventsPerBlock);
			check (count == 0,
			       "NEGATIVE CONTROL: no note-off for a note this voice never started");
		}

		// reset() is the DEACTIVATE path: silent, and deliberately
		// emitting nothing, because there is no block to put events in.
		{
			MidiClip clip;
			clip.notes.push_back ({ 0.0, 4.0, 60, 100 });
			clip.content = 4.0;

			MidiVoice voice;
			voice.start (0.0);
			voice.render (&clip, 4.0, 0.0, perSample, 1000, out, kMaxMidiEventsPerBlock);
			voice.reset ();

			check (! voice.playing (), "reset stops the voice");
			check (voice.allNotesOff (0, out, kMaxMidiEventsPerBlock) == 0,
			       "and forgets what was sounding, because nothing can be sent");
		}
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
