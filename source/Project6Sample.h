//------------------------------------------------------------------------
// Project6 - sample buffers, and the WAV reader that fills them
//
// SDK-FREE, like Project6Dsp.* and Project6Slots.*, and here the reason is
// mostly that a file format parser is the one part of a plug-in you can
// genuinely test: tests/WavTests.cpp builds wav files byte by byte in
// memory and reads them back, with no disk and no host in the way.
//
// THE PARSE IS SPLIT FROM THE FILE READ on purpose. parseWav() takes
// bytes, so every format, every malformation and every edge of the length
// cap can be exercised; loadWavFile() is the thin part that opens a file
// and hands its contents over, and is the only part a test cannot reach.
//
// Everything comes out as INTERLEAVED STEREO FLOAT at the file's own
// sample rate. Converting on load rather than on playback means the audio
// thread deals with exactly one layout however odd the file was, and the
// conversion happens once instead of on every block.
//------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Project6 {

/** Everything a loaded sample is converted to. */
constexpr int kSampleChannels = 2;

/** The longest file a slot will take.

    A cap is needed because a slot is filled by dropping whatever was
    under the pointer, and sixty-four accidental drops of an album side
    would be several gigabytes of resident memory. Sixty seconds at
    stereo float is about 21 MB per slot - generous for the loops this is
    for, and bounded. A longer file is REFUSED and says so, rather than
    being silently truncated to something that then loops wrongly. */
constexpr double kMaxSampleSeconds = 60.0;

//------------------------------------------------------------------------
// TEMPO DETECTION
//
// A loop dropped on a pad is only useful at the project's tempo, so the
// file has to admit what tempo it was recorded at. Two sources, in order
// of how much they can be trusted:
//
//   1. the "acid" chunk, which most loop libraries embed. It carries a
//      beat count, a tempo and - the part that matters most - a ONE-SHOT
//      FLAG saying "this is a hit, not a loop, do not stretch it".
//   2. failing that, the file's LENGTH, on the assumption that a loop is
//      a whole number of beats. That is a guess, so it is bounded: a beat
//      count is only accepted if the tempo it implies lands in a
//      plausible range, and the candidate closest to a reference tempo
//      wins.
//
// When neither works the tempo is left at zero and the file is played
// exactly as it arrived. A wrong guess would stretch a sample that was
// already right; leaving it alone is the failure that can be heard and
// corrected.
//------------------------------------------------------------------------

/** The window an INFERRED tempo has to land in to be believed.

    Nothing about a file's length says which of the many beat counts that
    divide it was intended - two bars at 90 and four bars at 180 are the
    same number of seconds. The range is what makes the guess decidable
    at all, and it is deliberately narrow: dance-music loops live inside
    it, and a file whose only plausible reading is 45 or 240 BPM is far
    likelier to be a one-shot or a bar of silence than a loop. */
constexpr double kMinInferredBpm = 70.0;
constexpr double kMaxInferredBpm = 180.0;

/** The tempo an inferred candidate is measured against when the caller
    does not say. Every candidate is inside the window above, so the
    reference is what breaks the tie between them; the project's own
    tempo is the right answer and this is the stand-in for "no project". */
constexpr double kReferenceBpm = 120.0;

/** A tempo outside this is not a tempo, whoever claims it. Applied to the
    "acid" chunk too, because a library that writes a zero or a 6000 into
    that field is not to be followed off a cliff. */
constexpr double kMinBelievableBpm = 20.0;
constexpr double kMaxBelievableBpm = 400.0;

//------------------------------------------------------------------------
/** Where a sample's tempo came from - or that it has none.

    Recorded rather than discarded because the panel says it out loud.
    "90 BPM (ACID chunk)" and "90 BPM (inferred)" mean very different
    things to someone wondering why a pad sounds wrong, and the second one
    is a hint to go and look at the file. */
enum class TempoSource
{
	None,       ///< no tempo known; the file plays exactly as it arrived
	AcidTempo,  ///< the "acid" chunk stated a tempo
	AcidBeats,  ///< the "acid" chunk stated a beat count; tempo from length
	Inferred    ///< guessed from the file's length and a plausible beat count
};

/** One short phrase, for tooltips. Never null. */
const char* tempoSourceText (TempoSource source);


//------------------------------------------------------------------------
/** What happened when a slot's file was read.

    A slot that will not play has to be able to SAY SO. The alternative -
    a slot that takes the drop, shows the name and does nothing when
    clicked - is the failure this project's notes keep coming back to: a
    control that looks live and is not. */
enum class SampleStatus
{
	Empty,              ///< no file in this slot
	Loaded,             ///< read, converted, ready
	Unreadable,         ///< the file could not be opened or read
	NotWave,            ///< not a RIFF/WAVE file at all
	UnsupportedFormat,  ///< a wav, but not a PCM or float layout we decode
	TooLong,            ///< longer than kMaxSampleSeconds
	NoFrames            ///< a wav with no audio in it
};

/** One short line, for the panel's tooltip. Never null. */
const char* sampleStatusText (SampleStatus status);

//------------------------------------------------------------------------
/** Decoded audio, ready to play.

    IMMUTABLE ONCE BUILT. The audio thread reads one of these through a
    bare pointer while the UI thread may be building the next; that is
    only safe because nothing ever modifies a buffer after it has been
    published. Load a new file, publish a new buffer, retire the old one -
    see Project6Processor. */
struct SampleBuffer
{
	/** Interleaved, kSampleChannels floats per frame. */
	std::vector<float> samples;
	int    frameCount = 0;
	double sourceRate = 44100.0;
	/** The largest absolute sample in the file, for the level report the
	    tests print and for anything that later wants to normalise. */
	float  peak = 0.f;

	/** The tempo this file was recorded at, or 0 when it is not known.
	    Never a fallback value: zero means "do not fit this", and a
	    plausible-looking default here would silently stretch every file
	    whose tempo could not be read. */
	double tempoBpm = 0.0;
	/** How many beats long the file was taken to be, when that is known.
	    Zero otherwise. Carried because it is what makes a detected tempo
	    checkable by eye - four beats at 120 is a two-second file. */
	int    beats = 0;
	/** The "acid" chunk's one-shot flag. A one-shot is a hit, not a loop:
	    fitting it to the project tempo would change the length of a snare
	    for no reason, so it is NEVER fitted whatever the pad is set to. */
	bool   oneShot = false;
	TempoSource tempoSource = TempoSource::None;

	int channels () const { return kSampleChannels; }
	/** True when this file may be stretched to the project's tempo: we
	    know what tempo it is, and it is not a one-shot. */
	bool fittable () const { return tempoBpm > 0.0 && !oneShot; }
	bool empty () const { return frameCount <= 0 || samples.empty (); }
	double seconds () const { return (sourceRate > 0.0) ? frameCount / sourceRate : 0.0; }
};

//------------------------------------------------------------------------
/** Parse RIFF/WAVE bytes into `out`.

    Handles the formats a sample library actually contains: PCM at 8, 16,
    24 and 32 bits, IEEE float at 32 and 64, mono or stereo or more, and
    WAVE_FORMAT_EXTENSIBLE, which is what a 24-bit file from most modern
    editors really is. Chunks other than "fmt " and "data" are skipped,
    including the odd-length ones that carry a pad byte the size field
    does not mention.

    `referenceBpm` only affects the INFERRED case - it is the tempo an
    ambiguous file is assumed to be nearest, and should be the project's
    own tempo when there is one. Pass 0 for kReferenceBpm.

    `out` is left empty unless the return is SampleStatus::Loaded. */
SampleStatus parseWav (const unsigned char* data, std::size_t size, SampleBuffer& out,
                       double referenceBpm = 0.0);

/** Read a file from disk and parse it.

    ON THE UI THREAD ONLY. It opens a file, allocates and decodes; none of
    those things may happen on the audio thread. */
SampleStatus loadWavFile (const std::string& path, SampleBuffer& out,
                          double referenceBpm = 0.0);

//------------------------------------------------------------------------
/** Guess a tempo from a duration, by assuming a whole number of beats.

    Exposed separately from parseWav because it is the part that is a
    GUESS, and a guess deserves its own tests. Returns 0 when no candidate
    beat count puts the tempo inside [kMinInferredBpm, kMaxInferredBpm];
    otherwise returns that tempo and, if `beatsOut` is not null, the beat
    count it came from.

    Candidates are the musical lengths a loop actually comes in - powers
    of two and their triple-time neighbours. Ties are broken by closeness
    to `referenceBpm` IN LOG SPACE, because tempo is a ratio: 60 and 240
    are equally far from 120, and measuring the distance linearly would
    make the faster half of the window win every time. */
double inferTempoFromLength (double seconds, double referenceBpm, int* beatsOut = nullptr);

//------------------------------------------------------------------------
} // namespace Project6
