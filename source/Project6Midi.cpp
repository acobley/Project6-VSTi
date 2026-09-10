//------------------------------------------------------------------------
// Project6 - the standard MIDI file reader, implementation
//
// SDK-free. Every multi-byte value is assembled from bytes, as in the WAV
// reader - but the other way round, because MIDI IS BIG-ENDIAN and WAV is
// little-endian. Getting that backwards is the classic first bug in an
// SMF reader and it does not announce itself: a division of 480 read the
// wrong way is 61440, and every note lands in the first hundredth of a
// bar with no error anywhere.
//------------------------------------------------------------------------

#include "Project6Midi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace Project6 {

namespace {

//------------------------------------------------------------------------
// BIG-ENDIAN readers. Bounds are the caller's business; every call below
// is guarded by an explicit size check first.
//------------------------------------------------------------------------
uint16_t readU16 (const unsigned char* p)
{
	return static_cast<uint16_t> ((static_cast<uint16_t> (p[0]) << 8) | p[1]);
}

uint32_t readU32 (const unsigned char* p)
{
	return (static_cast<uint32_t> (p[0]) << 24) | (static_cast<uint32_t> (p[1]) << 16)
	       | (static_cast<uint32_t> (p[2]) << 8) | static_cast<uint32_t> (p[3]);
}

bool tagIs (const unsigned char* p, const char* tag)
{
	for (int i = 0; i < 4; ++i)
		if (p[i] != static_cast<unsigned char> (tag[i]))
			return false;
	return true;
}

//------------------------------------------------------------------------
/** A VARIABLE-LENGTH QUANTITY: seven bits a byte, high bit set on every
    byte but the last.

    Returns false at the end of the data or on a quantity longer than four
    bytes, which is malformed - the format allows at most four, and a
    reader that kept going would follow a corrupt file until it ran off
    the end of the buffer.

    `at` is advanced past what was read either way, so a caller that gives
    up still knows where it got to. */
bool readVarLen (const unsigned char* data, std::size_t size, std::size_t& at, uint32_t& value)
{
	value = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (at >= size)
			return false;

		const unsigned char byte = data[at++];
		value = (value << 7) | static_cast<uint32_t> (byte & 0x7Fu);

		if ((byte & 0x80u) == 0u)
			return true;
	}
	return false;
}

/** How many data bytes follow a channel status byte. */
int channelMessageLength (unsigned char status)
{
	switch (status & 0xF0u)
	{
		case 0xC0:      // program change
		case 0xD0:      // channel pressure
			return 1;
		default:
			return 2;   // note off/on, poly pressure, control change, pitch bend
	}
}

//------------------------------------------------------------------------
/** A note that has been started and is waiting for its note-off.

    A LIST, NOT ONE SLOT PER PITCH. The same pitch can legally be struck
    again before the first is released, and a reader that kept one open
    note per (channel, note) would silently drop the overlap. Note-offs
    pair with the OLDEST matching note-on, which is what every sequencer
    does. */
struct OpenNote
{
	std::size_t   index;     ///< into the clip's note list
	unsigned char channel;
	unsigned char note;
};

} // namespace

//------------------------------------------------------------------------
double barQuartersFor (int numerator, int denominator)
{
	// A bar is `numerator` notes of 1/denominator each, and a quarter note
	// is 1/4 - so 4/4 is 4 quarters, 6/8 is 6 * (4/8) = 3, and 3/4 is 3.
	if (numerator <= 0 || denominator <= 0)
		return 4.0;

	return static_cast<double> (numerator) * 4.0 / static_cast<double> (denominator);
}

//------------------------------------------------------------------------
double loopLengthQuarters (double content, double barQuarters)
{
	if (!(barQuarters > 0.0))
		return (content > 0.0) ? content : 0.0;

	// A clip with nothing in it is still ONE BAR long rather than zero.
	// A loop of zero length would divide by zero in the player and, if it
	// did not, would repeat infinitely often per block.
	if (!(content > 0.0))
		return barQuarters;

	const double bars = content / barQuarters;

	// THE TOLERANCE IS WHAT MAKES "already a whole number of bars" TRUE.
	// A four-bar clip whose ticks divided out to 3.9999999996 bars is four
	// bars; without this it would be padded to five and every loop in the
	// bank would be a bar too long.
	constexpr double kBarTolerance = 1e-6;
	const double rounded = std::ceil (bars - kBarTolerance);

	return std::max (1.0, rounded) * barQuarters;
}

//------------------------------------------------------------------------
MidiStatus parseMidiFile (const unsigned char* data, std::size_t size, MidiClip& out)
{
	out = MidiClip ();

	// MThd, a length of 6, then format, track count and division.
	if (data == nullptr || size < 14 || !tagIs (data, "MThd"))
		return MidiStatus::NotMidi;

	const uint32_t headerLength = readU32 (data + 4);
	if (headerLength < 6 || size < 8 + headerLength)
		return MidiStatus::NotMidi;

	const uint16_t format   = readU16 (data + 8);
	const uint16_t trackCount = readU16 (data + 10);
	const int16_t  division = static_cast<int16_t> (readU16 (data + 12));

	// FORMAT 2 is a set of INDEPENDENT sequences, not one song. Merging
	// them onto one timeline would be inventing a piece of music that is
	// not in the file, so it is refused instead.
	if (format > 1)
		return MidiStatus::UnsupportedFormat;

	// A NEGATIVE DIVISION IS SMPTE: the file is timed in frames and
	// seconds and has no musical grid at all. Everything this plug-in
	// does with a MIDI file is musical, so there is nothing honest to do
	// with one.
	if (division <= 0)
		return MidiStatus::UnsupportedFormat;

	if (trackCount > kMaxMidiTracks)
		return MidiStatus::TooLong;

	const double ticksPerQuarter = static_cast<double> (division);

	std::vector<OpenNote> open;
	double contentTicks = 0.0;
	bool   haveSignature = false;

	// Tracks are MERGED. In a format 1 file the notes are spread across
	// them and every track's delta times start again from zero, so each
	// is walked from its own zero and the results share one timeline.
	std::size_t at = 8 + headerLength;
	int tracksRead = 0;

	while (at + 8 <= size && tracksRead < static_cast<int> (trackCount))
	{
		const uint32_t chunkLength = readU32 (data + at + 4);
		const std::size_t body = at + 8;

		// A chunk claiming more than the file holds is a TRUNCATED FILE,
		// not a licence to read past the end. What is actually there is
		// still read, so a file cut short still plays what survived.
		const std::size_t available = (body < size) ? (size - body) : 0;
		const std::size_t usable = std::min (static_cast<std::size_t> (chunkLength), available);
		const std::size_t end = body + usable;

		// Anything that is not an MTrk is skipped by its own length. The
		// spec says to do exactly this, so that a file carrying a chunk
		// type invented after this was written still reads.
		if (!tagIs (data + at, "MTrk"))
		{
			at = body + chunkLength;
			continue;
		}

		++tracksRead;

		std::size_t   cursor  = body;
		double        tick    = 0.0;
		unsigned char running = 0;

		while (cursor < end)
		{
			uint32_t delta = 0;
			if (!readVarLen (data, end, cursor, delta))
				break;
			tick += static_cast<double> (delta);

			if (cursor >= end)
				break;

			unsigned char status = data[cursor];

			if (status < 0x80u)
			{
				// RUNNING STATUS: the status byte is omitted and the
				// previous one still applies. Files from hardware
				// sequencers are full of it, and a reader that does not
				// handle it reads one note and then garbage.
				if (running == 0)
					break;
				status = running;
			}
			else
			{
				++cursor;
				// System messages CLEAR the running status; channel
				// messages set it.
				running = (status < 0xF0u) ? status : 0;
			}

			if (status == 0xFFu)
			{
				// META. Type byte, then a variable-length length.
				if (cursor >= end)
					break;
				const unsigned char type = data[cursor++];

				uint32_t length = 0;
				if (!readVarLen (data, end, cursor, length))
					break;
				if (cursor + length > end)
					break;

				if (type == 0x2Fu)
				{
					// End of track. The tail of the chunk is whatever the
					// file left there and is not read.
					cursor = end;
				}
				else if (type == 0x58u && length >= 2 && !haveSignature)
				{
					// The FIRST time signature only, and only to be shown.
					// The loop is rounded to the project's bar.
					out.sigNumerator = data[cursor];
					out.sigDenominator = 1 << data[cursor + 1];
					haveSignature = true;
					cursor += length;
				}
				else if (type == 0x51u && length >= 3 && out.fileTempoBpm <= 0.0)
				{
					// Microseconds per quarter note. READ AND DISCARDED -
					// see the top of the header. It is kept only so the
					// tooltip can say what the file thought it was.
					const uint32_t usPerQuarter =
						(static_cast<uint32_t> (data[cursor]) << 16)
						| (static_cast<uint32_t> (data[cursor + 1]) << 8)
						| static_cast<uint32_t> (data[cursor + 2]);
					if (usPerQuarter > 0)
						out.fileTempoBpm = 60000000.0 / static_cast<double> (usPerQuarter);
					cursor += length;
				}
				else
				{
					cursor += length;
				}
				continue;
			}

			if (status == 0xF0u || status == 0xF7u)
			{
				// SYSEX, skipped by its own declared length. Missing this
				// is the other classic SMF bug: a dump of a few hundred
				// bytes read as events turns the rest of the track into
				// noise.
				uint32_t length = 0;
				if (!readVarLen (data, end, cursor, length))
					break;
				if (cursor + length > end)
					break;
				cursor += length;
				continue;
			}

			const int dataBytes = channelMessageLength (status);
			if (cursor + static_cast<std::size_t> (dataBytes) > end)
				break;

			const unsigned char channel = status & 0x0Fu;
			const unsigned char first   = data[cursor];
			const unsigned char second  = (dataBytes > 1) ? data[cursor + 1] : 0;
			cursor += static_cast<std::size_t> (dataBytes);

			const unsigned char kind = status & 0xF0u;

			// NOTE ON WITH VELOCITY ZERO IS A NOTE OFF. Most files use it
			// in preference to a real note-off, because running status
			// then covers a whole passage; a reader that takes it at face
			// value leaves every note on for ever.
			const bool isNoteOff = (kind == 0x80u) || (kind == 0x90u && second == 0);
			const bool isNoteOn  = (kind == 0x90u && second > 0);

			if (isNoteOn)
			{
				if (static_cast<int> (out.notes.size ()) >= kMaxMidiNotes)
					return MidiStatus::TooLong;

				MidiNote note;
				note.start    = tick / ticksPerQuarter;
				note.end      = note.start;      // closed by its note-off
				note.note     = first & 0x7Fu;
				note.velocity = second & 0x7Fu;

				open.push_back ({ out.notes.size (), channel, note.note });
				out.notes.push_back (note);
			}
			else if (isNoteOff)
			{
				const unsigned char pitch = first & 0x7Fu;

				// THE OLDEST MATCHING NOTE-ON, which is what a sequencer
				// does and what makes overlapping strikes of one pitch
				// come out as two notes rather than one long one.
				const auto match = std::find_if (
					open.begin (), open.end (),
					[&] (const OpenNote& n) { return n.channel == channel && n.note == pitch; });

				if (match != open.end ())
				{
					out.notes[match->index].end = tick / ticksPerQuarter;
					open.erase (match);
				}
			}

			// Everything else - control change, program change, pitch
			// bend, aftertouch - is READ AND DROPPED. Notes only: there
			// is then nothing to unwind when a pad stops, no sustain
			// pedal to leave down and no program change firing on every
			// repeat. See the panel notes.
		}

		contentTicks = std::max (contentTicks, tick);
		at = body + chunkLength;
	}

	// A NOTE STILL HELD WHEN THE FILE ENDED is ended there. Leaving it
	// open would mean a note with no end, and the player would hold it
	// until something else stopped the pad.
	for (const OpenNote& note : open)
		out.notes[note.index].end = contentTicks / ticksPerQuarter;

	// Zero-length notes are DROPPED rather than emitted. A note-on and a
	// note-off at the same instant is swallowed by some instruments and
	// hung on by others, and it is never what was meant.
	out.notes.erase (
		std::remove_if (out.notes.begin (), out.notes.end (),
		                [] (const MidiNote& n) { return !(n.end > n.start); }),
		out.notes.end ());

	if (out.notes.empty ())
		return MidiStatus::NoNotes;

	// SORTED BY START. The player walks them in order and relies on it -
	// and after merging several tracks they are in no order at all.
	// Stable, so two notes at the same instant keep the order the file
	// put them in.
	std::stable_sort (out.notes.begin (), out.notes.end (),
	                  [] (const MidiNote& a, const MidiNote& b) { return a.start < b.start; });

	// The content is the later of the last note-off and the end of the
	// track: a file whose region runs past its last note keeps that tail,
	// and one whose last note runs past its end-of-track keeps the note.
	out.content = contentTicks / ticksPerQuarter;
	for (const MidiNote& note : out.notes)
		out.content = std::max (out.content, note.end);

	if (out.content > kMaxMidiQuarters)
		return MidiStatus::TooLong;

	return MidiStatus::Loaded;
}

//------------------------------------------------------------------------
MidiStatus loadMidiFile (const std::string& path, MidiClip& out)
{
	out = MidiClip ();

	if (path.empty ())
		return MidiStatus::NotMidi;

	std::ifstream file (path, std::ios::binary);
	if (!file)
		return MidiStatus::NotMidi;

	file.seekg (0, std::ios::end);
	const std::streamoff length = file.tellg ();
	file.seekg (0, std::ios::beg);

	if (length <= 0)
		return MidiStatus::NotMidi;

	// A hard ceiling on what is read into memory at all, before any
	// header is trusted. kMaxMidiQuarters is the real limit; this is only
	// so that a pathological "file" cannot ask for an arbitrary
	// allocation on the strength of its own size.
	constexpr std::streamoff kMaxFileBytes = 32ll * 1024ll * 1024ll;
	if (length > kMaxFileBytes)
		return MidiStatus::TooLong;

	std::vector<unsigned char> bytes (static_cast<std::size_t> (length));
	if (!file.read (reinterpret_cast<char*> (bytes.data ()), length))
		return MidiStatus::NotMidi;

	return parseMidiFile (bytes.data (), bytes.size (), out);
}

//------------------------------------------------------------------------
// MidiVoice
//------------------------------------------------------------------------

void MidiVoice::reset ()
{
	mPlaying  = false;
	mStartPpq = 0.0;
	mPosition = 0.0;
	mHaveLast = false;
	mProgress.store (0.f, std::memory_order_relaxed);

	// NOTHING EMITTED. This is the deactivate path, where there is no
	// block to put note-offs in; the host silences what it is driving.
	// Every other way a pad goes quiet goes through allNotesOff.
	for (unsigned char& count : mSounding)
		count = 0;
}

//------------------------------------------------------------------------
void MidiVoice::start (double atPpq)
{
	mPlaying  = true;
	mStartPpq = atPpq;
	mPosition = 0.0;
	mProgress.store (0.f, std::memory_order_relaxed);

	// The jump detector is armed by the first render after this, not
	// here: this IS the jump, and it is a deliberate one.
	mHaveLast = false;
}

//------------------------------------------------------------------------
void MidiVoice::emitOn (MidiEventOut* out, int maxOut, int& count, int offset,
                        unsigned char note, unsigned char velocity)
{
	if (note > 127 || count >= maxOut)
		return;

	out[count].sampleOffset = offset;
	out[count].noteOn       = true;
	out[count].note         = note;
	out[count].velocity     = velocity;
	++count;

	// Saturating. A pitch struck 255 times without a release is not
	// music, and wrapping the count to zero would lose every off.
	if (mSounding[note] < 255)
		++mSounding[note];
}

//------------------------------------------------------------------------
void MidiVoice::emitOff (MidiEventOut* out, int maxOut, int& count, int offset,
                         unsigned char note)
{
	// NOTHING SOUNDING, NOTHING TO STOP. This is what keeps a pad that
	// launched half way through its loop from sending an orphan note-off
	// for a note it never started - which some instruments answer by
	// cutting off a note another pad is playing.
	if (note > 127 || mSounding[note] == 0 || count >= maxOut)
		return;

	out[count].sampleOffset = offset;
	out[count].noteOn       = false;
	out[count].note         = note;
	out[count].velocity     = 0;
	++count;

	--mSounding[note];
}

//------------------------------------------------------------------------
int MidiVoice::allNotesOff (int sampleOffset, MidiEventOut* out, int maxOut)
{
	int count = 0;

	// ONE OFF PER OUTSTANDING ON, not one per pitch: a pitch struck twice
	// and released once is still sounding once, and an instrument that
	// counts its voices the same way needs the second.
	for (int note = 0; note < 128; ++note)
	{
		while (mSounding[note] > 0 && count < maxOut)
			emitOff (out, maxOut, count, sampleOffset, static_cast<unsigned char> (note));

		// If the buffer filled, the count is left standing so the next
		// block finishes the job rather than forgetting it.
		if (count >= maxOut)
			break;
	}

	mPlaying  = false;
	mPosition = 0.0;
	mHaveLast = false;
	mProgress.store (0.f, std::memory_order_relaxed);
	return count;
}

//------------------------------------------------------------------------
int MidiVoice::render (const MidiClip* clip, double loopLength, double blockStartPpq,
                       double quartersPerSample, int numSamples, bool loop,
                       MidiEventOut* out, int maxOut)
{
	if (out == nullptr || maxOut <= 0 || numSamples <= 0)
		return 0;

	// THE FILE WENT AWAY while the pad was running - emptied, or replaced
	// by one that failed to load. Stop rather than reading a null clip,
	// and stop with the note-offs rather than dead.
	if (clip == nullptr || clip->empty () || !(loopLength > 0.0)
	    || !(quartersPerSample > 0.0))
	{
		return allNotesOff (0, out, maxOut);
	}

	if (!mPlaying)
		return 0;

	const double blockQuarters = quartersPerSample * static_cast<double> (numSamples);

	// A HOST THAT JUMPED. The playhead was dragged, or a cycle wrapped,
	// and this block does not continue the last one. Everything sounding
	// is turned off at sample 0 and the loop carries on from wherever the
	// project now is - which is the whole point of locking the loop to
	// the timeline rather than to a sample count.
	int count = 0;
	if (mHaveLast && std::fabs (blockStartPpq - mLastPpq) > blockQuarters + 1e-6)
	{
		for (int note = 0; note < 128; ++note)
			while (mSounding[note] > 0 && count < maxOut)
				emitOff (out, maxOut, count, 0, static_cast<unsigned char> (note));
	}

	mLastPpq  = blockStartPpq + blockQuarters;
	mHaveLast = true;

	// Where this block sits relative to the pad's own start. NEGATIVE
	// when the pad launches part way through this very block, which is
	// the normal case: a grid line lands at some sample inside it.
	double from = blockStartPpq - mStartPpq;
	double to   = from + blockQuarters;

	// THE PROJECT IS ENTIRELY BEHIND THIS PAD'S LAUNCH POINT - somebody
	// rewound past it, or a cycle wrapped to a point before it.
	//
	// This used to return, and the pad went silent AND STAYED SILENT: not
	// stopped, still playing, still counted as launched, simply never
	// emitting anything again until the project crawled back past the
	// point it had been launched at. It was found in a session, not here,
	// which is what section 9 of the tests is for.
	//
	// THE LOOP REPEATS IN BOTH DIRECTIONS. It is anchored at the launch
	// point rather than started there, so rewinding plays the same loop
	// in the same phase - which is what "locked to the project's
	// timeline" has to mean if it is to mean anything.
	//
	// Only when the whole block is behind it, so that a pad launching
	// PART WAY THROUGH this block - `from` a fraction negative, the
	// normal case - still starts at its own sample rather than being
	// wrapped round to the end of the loop.
	if (to <= 0.0)
	{
		from = std::fmod (from, loopLength);
		if (from < 0.0)
			from += loopLength;
		to = from + blockQuarters;
	}

	// The sample a loop-time lands on. Clamped, because a rounding edge
	// must not put an event outside the block - a host is entitled to
	// reject that, and some drop the whole list.
	const auto sampleFor = [&] (double atQuarters)
	{
		const double offset = (atQuarters - from) / quartersPerSample;
		const int rounded = static_cast<int> (offset + 0.5);
		return std::min (numSamples - 1, std::max (0, rounded));
	};

	// A ONE-SHOT STOPS AT THE END OF ITS ONE PASS. The block is clipped
	// to the loop end, everything after it is not played at all, and the
	// voice is stopped below - so nothing has to notice the wrap and
	// nothing can slip through into a second pass.
	const bool endsHere = !loop && (to >= loopLength);
	if (endsHere)
		to = loopLength;

	// Walked in pieces that never cross a loop boundary, so that the
	// truncation at the loop end is a property of the piece rather than
	// something to test for note by note.
	double cursor = std::max (0.0, from);

	// A BOUND ON THE LOOP ITSELF. A very short loop in a very long block
	// would otherwise repeat thousands of times and emit thousands of
	// events; the buffer would stop it, but only after the work was done.
	for (int pass = 0; pass < 64 && cursor < to; ++pass)
	{
		const double local = std::fmod (cursor, loopLength);
		const double piece = std::min (to - cursor, loopLength - local);
		const double localEnd = local + piece;

		for (const MidiNote& note : clip->notes)
		{
			// Sorted by start, so once past the end of this piece there
			// is nothing further to find.
			if (note.start >= localEnd)
				break;

			// A NOTE IS CUT OFF AT THE LOOP END. Anything the file lets
			// run past the boundary ends there instead, so the loop is
			// self-contained and can repeat for ever without notes
			// piling up on each repetition.
			const double noteEnd = std::min (note.end, loopLength);

			// BOTH HALF-OPEN, and both by the same rule, so an event
			// falling exactly on a block boundary belongs to the block
			// that starts there and not to the one that ends there. The
			// other convention puts every off one sample early and is a
			// different rule for ons and offs, which is the kind of
			// asymmetry that hides an off-by-one for years.
			if (note.start >= local && note.start < localEnd)
				emitOn (out, maxOut, count, sampleFor (cursor + (note.start - local)),
				        note.note, note.velocity);

			if (noteEnd >= local && noteEnd < localEnd)
				emitOff (out, maxOut, count, sampleFor (cursor + (noteEnd - local)),
				         note.note);
		}

		cursor += piece;

		// THE TRUNCATION, stated plainly rather than hidden in an
		// interval's edge. A piece that reached the loop end is the loop
		// end, and everything still sounding stops there - which is what
		// makes the loop self-contained and able to repeat for ever
		// without notes piling up on every pass.
		//
		// It is also the backstop: an off that went missing for any other
		// reason is caught here within one repetition rather than hanging.
		if (local + piece >= loopLength - 1e-9)
		{
			const int at = sampleFor (cursor);
			for (int note = 0; note < 128; ++note)
				while (mSounding[note] > 0 && count < maxOut)
					emitOff (out, maxOut, count, at, static_cast<unsigned char> (note));
		}
	}

	if (endsHere)
	{
		// The pass is over. allNotesOff is what stops the voice, and it
		// catches anything the boundary flush above did not - which is
		// the same backstop a looping pad gets on every repetition and
		// a one-shot only gets here.
		const int at = std::min (numSamples - 1,
		                         std::max (0, static_cast<int> ((to - from) / quartersPerSample)));
		count += allNotesOff (at, out + count, maxOut - count);
		return count;
	}

	mPosition = std::fmod (std::max (0.0, to), loopLength);
	mProgress.store (static_cast<float> (mPosition / loopLength),
	                 std::memory_order_relaxed);
	return count;
}

//------------------------------------------------------------------------
} // namespace Project6
