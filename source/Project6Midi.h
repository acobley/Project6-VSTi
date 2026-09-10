//------------------------------------------------------------------------
// Project6 - MIDI clips, and the standard MIDI file reader that fills them
//
// SDK-FREE, like Project6Sample.*, Project6Dsp.* and Project6Stretch.*,
// and for the same reason it mattered there: a file format parser is the
// one part of a plug-in that can genuinely be tested, and
// tests/MidiTests.cpp builds standard MIDI files BYTE BY BYTE in memory
// with no disk and no host in the way.
//
// A SLOT NOW HOLDS EITHER KIND OF FILE. A .wav pad plays audio into its
// row's audio bus; a .mid pad plays notes into its row's EVENT bus. The
// launch rules, the bar-line quantise, the drag-and-drop and the state
// stream do not care which - a pad is a pad.
//
//------------------------------------------------------------------------
// EVERYTHING HERE IS MEASURED IN QUARTER NOTES.
//
// That is the single decision this file turns on, and it is why the
// tempo-fitting the audio pads need has no counterpart here at all.
//
// A .mid file carries its own tempo in a meta event, and a naive reader
// converts its ticks to seconds using it. Do that and every MIDI pad
// needs the same varispeed-or-stretch machinery a sample does, and it is
// all waste: notes have no waveform to resample. Converting ticks to
// QUARTER NOTES at load instead throws the file's tempo away as the
// irrelevance it is, and playback becomes a matter of following the
// host's own musical position. A project at 90 BPM plays the loop at 90;
// change it to 140 mid-bar and the loop is at 140 from that sample on,
// with no fitting, no artefacts and nothing to configure.
//
// The file's tempo meta event is therefore READ AND DISCARDED, and its
// time signature is kept only to be shown.
//------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace Project6 {

//------------------------------------------------------------------------
// The caps.
//
// A slot is filled by dropping whatever was under the pointer, so every
// one of these exists to bound what a mis-drop can cost. They are
// generous for the loops this is for and finite for everything else.
//------------------------------------------------------------------------

/** The longest loop a slot will take, in quarter notes. Sixty-four bars
    of 4/4 - far longer than anything anyone launches from a pad, and a
    hard stop on a file that claims to be a symphony. */
constexpr double kMaxMidiQuarters = 256.0;

/** How many notes, across every track merged together. */
constexpr int kMaxMidiNotes = 20000;

/** How many tracks a file may claim. Format 1 files from a DAW have a
    handful; a header claiming sixty thousand is a corrupt header. */
constexpr int kMaxMidiTracks = 256;

//------------------------------------------------------------------------
/** The MIDI channel a ROW's notes go out on.

    Row A is channel 1 and row H is channel 8, so the eight rows are eight
    parts even in a host that only exposes the first of the plug-in's MIDI
    outputs - see the merged bus in Project6Processor::initialize.

    TWO FUNCTIONS FOR ONE FACT, because the two callers count differently
    and one of them has to be wrong on its own terms if they share a
    number. VST3 counts channels from ZERO in Event::noteOn.channel;
    people count them from ONE, and the label on the panel is read by a
    person. Deriving the second from the first is what stops the panel
    from saying "channel 3" while the events go out on 4.

    The identity mapping is deliberate and worth stating: a row IS its
    channel, and the day that stops being true this is the single line
    that changes. */
constexpr int midiChannelForRow (int row) { return row; }

/** The same channel as a person counts it, for the panel to print. */
constexpr int midiChannelNumberForRow (int row) { return midiChannelForRow (row) + 1; }

//------------------------------------------------------------------------
/** One note, in quarter notes from the start of the clip.

    NO CHANNEL. Every pad's notes go out on ITS ROW'S channel - row A is
    channel 1, row H is channel 8 - so that the eight rows are eight
    parts even in a host that only exposes one of the plug-in's MIDI
    outputs. Keeping the file's own channel here would be keeping a number
    that is then overwritten, and a multi-channel file is merged onto its
    row's channel rather than half-honoured.

    `end` is always greater than `start`: a zero-length note is dropped by
    the reader rather than emitted as a note-on and note-off at the same
    instant, which some instruments swallow and others hang on. */
struct MidiNote
{
	double        start    = 0.0;   ///< quarter notes from the clip's start
	double        end      = 0.0;   ///< quarter notes; always > start
	unsigned char note     = 60;    ///< 0..127
	unsigned char velocity = 100;   ///< 1..127; a zero-velocity note-on is a note-off
};

//------------------------------------------------------------------------
/** A decoded MIDI file, ready to play.

    IMMUTABLE ONCE BUILT, exactly like SampleBuffer and for exactly the
    same reason: the audio thread reads one of these through a bare
    pointer while the UI thread may be building the next. */
struct MidiClip
{
	/** Sorted by `start`. The player walks them in order and relies on
	    it. */
	std::vector<MidiNote> notes;

	/** Where the file itself ends, in quarter notes - the later of its
	    last note-off and its end-of-track. NOT the loop length: see
	    loopLengthQuarters below, which rounds this up to a whole bar of
	    whatever the project is in. */
	double content = 0.0;

	/** The file's own time signature, kept only to be SHOWN. The loop is
	    rounded to the PROJECT's bar, because that is the grid the pad
	    launches on and a loop measured against any other would drift
	    against everything else on the panel. */
	int sigNumerator   = 4;
	int sigDenominator = 4;

	/** What the file said its tempo was, or 0. Kept for the tooltip and
	    used for nothing: see the note at the top of this file. */
	double fileTempoBpm = 0.0;

	bool empty () const { return notes.empty (); }
	int  noteCount () const { return static_cast<int> (notes.size ()); }
};

//------------------------------------------------------------------------
/** How long the loop actually is: `content` rounded UP to a whole bar.

    THE REQUEST THIS FILE EXISTS FOR. A file that ends half way through
    its last bar - which is most files, because a DAW exports the region
    you selected - would otherwise loop early and put every repetition
    one beat further out of step with the project. Rounding up means the
    tail of the bar is silence and the next repetition lands on a bar
    line.

    `barQuarters` is the PROJECT's bar, not the file's, and it is applied
    HERE rather than baked into the clip at load: a project whose time
    signature changes changes the loop, without anything being reloaded.
    The same lesson the tempo fit taught - see Project6Stretch.h.

    A file that is already an exact number of bars is left alone. The
    tolerance is what makes that true in floating point: a clip that came
    out at 3.9999999996 bars is four bars, not five. */
double loopLengthQuarters (double content, double barQuarters);

/** The bar length in quarter notes for a time signature.

    THE SHARED FUNCTION. 4/4 is 4 quarters, 6/8 is 3, 3/4 is 3. The
    processor rounds loops with it and the panel's tooltip reports with
    it; a second copy in either would be a second opinion about where the
    bar line is. */
double barQuartersFor (int numerator, int denominator);

//------------------------------------------------------------------------
/** Parse standard MIDI file bytes into `out`.

    Handles what a loop library and a DAW export actually contain: format
    0 (one track) and format 1 (several tracks that are one song, merged
    here onto one timeline), running status, note-on with velocity zero
    meaning note-off, SysEx and meta events skipped by their own declared
    lengths, and a note still held when the track ends.

    REFUSED, rather than half-read:

      * SMPTE division - a negative division means the file is timed in
        real seconds and frames, which cannot be looped to a bar of a
        tempo it does not follow;
      * format 2 - independent sequences, not one song, so merging them
        onto one timeline would be inventing a piece of music;
      * a file longer than kMaxMidiQuarters, or with more notes than
        kMaxMidiNotes.

    `out` is left empty unless the return is MidiStatus::Loaded. */
enum class MidiStatus
{
	Loaded,             ///< read, merged, ready
	NotMidi,            ///< not an MThd file at all
	UnsupportedFormat,  ///< SMPTE division, or format 2
	TooLong,            ///< longer than kMaxMidiQuarters, or too many notes
	NoNotes             ///< a valid MIDI file with nothing to play in it
};

MidiStatus parseMidiFile (const unsigned char* data, std::size_t size, MidiClip& out);

/** Read a file from disk and parse it.

    ON THE UI THREAD ONLY, like loadWavFile: it opens a file, allocates
    and decodes, and none of those may happen on the audio thread. */
MidiStatus loadMidiFile (const std::string& path, MidiClip& out);

//------------------------------------------------------------------------
/** One event on its way out of a pad.

    Notes only - see the parser. There is nothing else in here because
    there is nothing else to send: no controller to reset, no program
    change to fire on every repeat, and nothing left behind when a pad
    stops. */
struct MidiEventOut
{
	int           sampleOffset = 0;
	bool          noteOn       = false;
	unsigned char note         = 60;
	unsigned char velocity     = 0;   ///< 0 on a note-off
};

/** How many events one pad may emit in one block.

    A bound, not a budget: a block is a few milliseconds and a loop that
    wanted more than this in one is not music. It matters because the
    caller's buffer is on the STACK - the audio thread does not allocate. */
constexpr int kMaxMidiEventsPerBlock = 192;

//------------------------------------------------------------------------
/** One pad's MIDI playhead.

    THE PLAYHEAD IS MUSICAL, not in samples. A pad launched on a grid line
    remembers the project position it started at, and where it is in its
    loop is then simply how far the project has moved since - which means
    it can never drift, survives a tempo change with no arithmetic at all,
    and follows the host when somebody drags the playhead.

    THE ONE THING THIS CLASS MUST NEVER DO is leave a note on. Every note
    it starts is counted, and everything counted is turned off when the
    loop reaches the note's end, when the loop wraps past it, when the pad
    is stopped, when the transport stops, when the host jumps, when the
    file is taken away and when the plug-in is bypassed. A hanging note is
    the failure mode of every MIDI looper ever written, and the counting
    is what makes each of those a single call to allNotesOff rather than
    a separate thing to remember. */
class MidiVoice
{
public:
	/** Silent, playing nothing, and NOTHING EMITTED. For the case where
	    there is no block to put note-offs in - a deactivate, where the
	    host is responsible for silencing what it is driving. */
	void reset ();

	bool playing () const { return mPlaying; }

	/** Where the loop is, in quarter notes from its start. */
	double position () const { return mPosition; }

	/** The same as a fraction, for the bar the pad draws.

	    WRITTEN BY THE AUDIO THREAD and read by the UI thread, so it is an
	    atomic - and a relaxed one, exactly as the sample voices' is: a
	    bar one block out of date is right to within eleven milliseconds,
	    and nothing else depends on it. Zero when the pad is not playing,
	    so a bar never lingers on a stopped one. */
	float progress () const { return mProgress.load (std::memory_order_relaxed); }

	/** Begin at loop position 0 at the project position `atPpq`, which is
	    the grid line the pad launched on. May be part way through the
	    block that is about to be rendered - render() works out the sample
	    from the difference. */
	void start (double atPpq);

	/** Emit note-offs for everything this voice has sounding, at
	    `sampleOffset`, and stop. Returns how many were written.

	    Call it for every reason a pad can go quiet. It is idempotent: a
	    voice with nothing sounding writes nothing. */
	int allNotesOff (int sampleOffset, MidiEventOut* out, int maxOut);

	/** Walk one block and emit what falls inside it.

	    `blockStartPpq` is the project position at sample 0 of this block
	    and `quartersPerSample` is how fast it moves - both come from the
	    host's own context, so the pad follows the project's tempo without
	    ever being told what it is.

	    Returns how many events were written. When the buffer fills, note
	    ONS stop being written and note-offs go on being written: a
	    dropped note-on is a note nobody hears, and a dropped note-off is
	    a note nobody can stop. */
	int render (const MidiClip* clip, double loopLength, double blockStartPpq,
	            double quartersPerSample, int numSamples, MidiEventOut* out, int maxOut);

private:
	/** Emit, and count. */
	void emitOn (MidiEventOut* out, int maxOut, int& count, int offset,
	             unsigned char note, unsigned char velocity);
	void emitOff (MidiEventOut* out, int maxOut, int& count, int offset, unsigned char note);

	bool   mPlaying   = false;
	double mStartPpq  = 0.0;
	double mPosition  = 0.0;

	/** Where the previous block ended, so a host that JUMPS can be told
	    apart from one that is playing on. A locate with notes held would
	    otherwise leave them held for ever. */
	double mLastPpq   = 0.0;
	bool   mHaveLast  = false;

	/** How many note-ons this voice has sent for each pitch and not yet
	    matched with an off. A COUNT rather than a flag: one pitch can be
	    struck again before the first is released, and a flag would lose
	    the second off and hang the note. */
	unsigned char mSounding[128] = {};

	std::atomic<float> mProgress { 0.f };
};

//------------------------------------------------------------------------
} // namespace Project6
