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

	int channels () const { return kSampleChannels; }
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

    `out` is left empty unless the return is SampleStatus::Loaded. */
SampleStatus parseWav (const unsigned char* data, std::size_t size, SampleBuffer& out);

/** Read a file from disk and parse it.

    ON THE UI THREAD ONLY. It opens a file, allocates and decodes; none of
    those things may happen on the audio thread. */
SampleStatus loadWavFile (const std::string& path, SampleBuffer& out);

//------------------------------------------------------------------------
} // namespace Project6
