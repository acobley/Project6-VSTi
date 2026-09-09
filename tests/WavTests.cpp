//------------------------------------------------------------------------
// Project6 - WAV reader tests
//
// SDK-FREE, and disk-free: every file here is built BYTE BY BYTE in
// memory and handed to parseWav, which is the whole reason the parse is
// split from the file read. A test that needed real files on disk could
// not cover a truncated chunk, an odd-length metadata chunk or a
// four-channel file without somebody first finding one.
//
//     c++ -std=c++17 -O2 -Wall -Isource tests/WavTests.cpp
//         source/Project6Sample.cpp -o /tmp/wavtests && /tmp/wavtests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//------------------------------------------------------------------------

#include "Project6Sample.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
// A wav builder. Deliberately NOT sharing code with the parser - a test
// that used the reader's own idea of the layout would agree with it about
// a mistake.
//------------------------------------------------------------------------
struct Bytes
{
	std::vector<unsigned char> data;

	void u8 (int v)         { data.push_back (static_cast<unsigned char> (v & 0xFF)); }
	void u16 (int v)        { u8 (v); u8 (v >> 8); }
	void u32 (uint32_t v)   { u8 (static_cast<int> (v)); u8 (static_cast<int> (v >> 8));
	                          u8 (static_cast<int> (v >> 16)); u8 (static_cast<int> (v >> 24)); }
	void tag (const char* t){ for (int i = 0; i < 4; ++i) u8 (t[i]); }
	void f32 (float v)      { uint32_t bits = 0; std::memcpy (&bits, &v, sizeof (bits)); u32 (bits); }
	void raw (const std::vector<unsigned char>& b) { data.insert (data.end (), b.begin (), b.end ()); }
};

struct WavSpec
{
	uint16_t format   = 1;          // 1 PCM, 3 float, 0xFFFE extensible
	uint16_t subFormat = 1;         // only read when format is extensible
	uint16_t channels = 2;
	uint32_t rate     = 44100;
	uint16_t bits     = 16;
	std::vector<unsigned char> audio;
	/** An odd-length chunk before "data", to prove the pad byte is
	    handled - metadata chunks are odd all the time. */
	bool oddChunkBefore = false;
	bool omitData       = false;
	/** Claim more data bytes than are actually present. */
	bool truncateData   = false;
	/** An "acid" chunk, written by hand to the published layout rather
	    than by anything the reader shares. */
	bool     hasAcid   = false;
	uint32_t acidFlags = 0;
	uint32_t acidBeats = 0;
	float    acidTempo = 0.f;
};

std::vector<unsigned char> buildWav (const WavSpec& spec)
{
	Bytes fmt;
	fmt.u16 (spec.format);
	fmt.u16 (spec.channels);
	fmt.u32 (spec.rate);
	fmt.u32 (spec.rate * spec.channels * (spec.bits / 8u));       // byte rate
	fmt.u16 (static_cast<int> (spec.channels * (spec.bits / 8u))); // block align
	fmt.u16 (spec.bits);
	if (spec.format == 0xFFFE)
	{
		fmt.u16 (22);                       // cbSize
		fmt.u16 (spec.bits);                // valid bits
		fmt.u32 (0);                        // channel mask
		fmt.u16 (spec.subFormat);           // the GUID's first two bytes
		for (int i = 0; i < 14; ++i)        // the rest of the GUID
			fmt.u8 (0);
	}

	Bytes body;
	body.tag ("WAVE");

	body.tag ("fmt ");
	body.u32 (static_cast<uint32_t> (fmt.data.size ()));
	body.raw (fmt.data);

	if (spec.oddChunkBefore)
	{
		const std::vector<unsigned char> note = { 'h', 'i', '!' };   // three bytes: ODD
		body.tag ("LIST");
		body.u32 (3);
		body.raw (note);
		body.u8 (0);                        // the pad byte the size does not count
	}

	if (spec.hasAcid)
	{
		// Twenty-four bytes, in the order the chunk has had since Sonic
		// Foundry: flags, root note, two unknowns, beats, meter, tempo.
		Bytes acid;
		acid.u32 (spec.acidFlags);
		acid.u16 (60);                      // root note
		acid.u16 (0x8000);                  // the constant unknown
		acid.f32 (0.f);                     // the other constant unknown
		acid.u32 (spec.acidBeats);
		acid.u16 (4);                       // meter denominator
		acid.u16 (4);                       // meter numerator
		acid.f32 (spec.acidTempo);

		body.tag ("acid");
		body.u32 (static_cast<uint32_t> (acid.data.size ()));
		body.raw (acid.data);
	}

	if (!spec.omitData)
	{
		body.tag ("data");
		body.u32 (static_cast<uint32_t> (spec.audio.size ()
		                                 + (spec.truncateData ? 1000u : 0u)));
		body.raw (spec.audio);
	}

	Bytes file;
	file.tag ("RIFF");
	file.u32 (static_cast<uint32_t> (body.data.size ()));
	file.raw (body.data);
	return file.data;
}

SampleStatus parse (const std::vector<unsigned char>& bytes, SampleBuffer& out,
                    double referenceBpm = 0.0)
{
	return parseWav (bytes.data (), bytes.size (), out, referenceBpm);
}

/** A file of an EXACT duration, so the tempo arithmetic can be done by
    hand. One channel of 8-bit silence at 1000 frames a second: a file of
    `seconds` seconds is `seconds * 1000` bytes long, and nothing about
    the audio matters to a length. */
WavSpec silenceOfSeconds (double seconds)
{
	WavSpec spec;
	spec.channels = 1;
	spec.bits     = 8;
	spec.rate     = 1000;
	spec.audio.assign (static_cast<std::size_t> (seconds * 1000.0 + 0.5), 128);
	return spec;
}

/** Two stereo frames of 16-bit PCM: full negative, silence, full positive,
    half positive. */
std::vector<unsigned char> pcm16Frames ()
{
	Bytes b;
	b.u16 (0x8000); b.u16 (0x0000);      // frame 0: L -1.0,  R 0.0
	b.u16 (0x7FFF); b.u16 (0x4000);      // frame 1: L +32767, R +0.5
	return b.data;
}

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - WAV reader tests\n");

	//--------------------------------------------------------------------
	section ("1. 16-bit stereo PCM, the common case");
	//--------------------------------------------------------------------
	{
		WavSpec spec;
		spec.audio = pcm16Frames ();

		SampleBuffer buffer;
		check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "it loads");
		check (buffer.frameCount == 2,             "two frames");
		check (close (buffer.sourceRate, 44100.0, 0.0), "at the file's own rate");
		check (buffer.samples.size () == 4,        "four interleaved stereo floats");

		// -32768 / 32768 is exactly -1. Dividing by 32767 instead would
		// give -1.00003 and clip a file that was already at full scale.
		check (close (buffer.samples[0], -1.0, 1e-7),  "-32768 decodes to exactly -1.0");
		check (close (buffer.samples[1],  0.0, 1e-7),  "0 decodes to silence");
		check (close (buffer.samples[2],  0.999969, 1e-5), "+32767 decodes just under +1");
		check (close (buffer.samples[3],  0.5, 1e-6),  "+16384 decodes to +0.5");

		check (close (buffer.peak, 1.0, 1e-6),  "the peak is the loudest sample in the file");
		check (close (buffer.seconds (), 2.0 / 44100.0, 1e-12), "and the length is in seconds");
	}

	//--------------------------------------------------------------------
	section ("2. The other depths");
	//--------------------------------------------------------------------
	{
		// 8-bit is UNSIGNED, alone among the PCM depths.
		{
			WavSpec spec;
			spec.channels = 1;
			spec.bits = 8;
			spec.audio = { 0, 128, 255 };

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "8-bit loads");
			check (close (buffer.samples[0], -1.0, 1e-6),  "0 is full negative");
			check (close (buffer.samples[2],  0.0, 1e-6),  "128 is silence");
			check (close (buffer.samples[4],  0.9922, 1e-3), "255 is full positive");

			// NEGATIVE CONTROL for the sign rule itself. Read as signed,
			// a byte of 0 would be silence - which is what turns a quiet
			// 8-bit file into a loud square wave.
			check (! close (buffer.samples[0], 0.0, 1e-6),
			       "NEGATIVE CONTROL: 8-bit is not read as signed");
		}

		// 24-bit, which is what most sample libraries actually are.
		{
			WavSpec spec;
			spec.channels = 1;
			spec.bits = 24;
			// 0x800000 = full negative, 0x000000 = silence, 0x400000 = +0.5
			spec.audio = { 0x00, 0x00, 0x80,  0x00, 0x00, 0x00,  0x00, 0x00, 0x40 };

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "24-bit loads");
			check (close (buffer.samples[0], -1.0, 1e-6), "sign-extended to full negative");
			check (close (buffer.samples[2],  0.0, 1e-6), "silence");
			check (close (buffer.samples[4],  0.5, 1e-6), "and +0.5");
		}

		// 32-bit integer.
		{
			WavSpec spec;
			spec.channels = 1;
			spec.bits = 32;
			Bytes b;
			b.u32 (0x80000000u);    // -1.0
			b.u32 (0x40000000u);    // +0.5
			spec.audio = b.data;

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "32-bit int loads");
			check (close (buffer.samples[0], -1.0, 1e-6), "full negative");
			check (close (buffer.samples[2],  0.5, 1e-6), "and +0.5");
		}

		// 32-bit float.
		{
			WavSpec spec;
			spec.format = 3;
			spec.channels = 1;
			spec.bits = 32;
			Bytes b;
			const float values[] = { -0.25f, 0.75f };
			for (float v : values)
			{
				uint32_t bits = 0;
				std::memcpy (&bits, &v, sizeof (bits));
				b.u32 (bits);
			}
			spec.audio = b.data;

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "float32 loads");
			check (close (buffer.samples[0], -0.25, 1e-7), "and comes back unchanged");
			check (close (buffer.samples[2],  0.75, 1e-7), "at both samples");
		}

		// 64-bit float.
		{
			WavSpec spec;
			spec.format = 3;
			spec.channels = 1;
			spec.bits = 64;
			Bytes b;
			const double values[] = { -0.125, 0.625 };
			for (double v : values)
			{
				uint64_t bits = 0;
				std::memcpy (&bits, &v, sizeof (bits));
				for (int i = 0; i < 8; ++i)
					b.u8 (static_cast<int> ((bits >> (i * 8)) & 0xFF));
			}
			spec.audio = b.data;

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "float64 loads");
			check (close (buffer.samples[0], -0.125, 1e-7), "and narrows to float cleanly");
			check (close (buffer.samples[2],  0.625, 1e-7), "at both samples");
		}
	}

	//--------------------------------------------------------------------
	section ("3. WAVE_FORMAT_EXTENSIBLE - what a 24-bit file usually is");
	//--------------------------------------------------------------------
	{
		WavSpec spec;
		spec.format = 0xFFFE;
		spec.subFormat = 1;             // PCM, in the SubFormat GUID
		spec.channels = 1;
		spec.bits = 16;
		Bytes b; b.u16 (0x4000);
		spec.audio = b.data;

		SampleBuffer buffer;
		check (parse (buildWav (spec), buffer) == SampleStatus::Loaded,
		       "an extensible header is unwrapped to its real format");
		check (close (buffer.samples[0], 0.5, 1e-6), "and decoded by it");

		// The same wrapper round a format we cannot decode must still be
		// refused - unwrapping must not become "assume PCM".
		spec.subFormat = 0x0011;        // ADPCM
		SampleBuffer other;
		check (parse (buildWav (spec), other) == SampleStatus::UnsupportedFormat,
		       "NEGATIVE CONTROL: an extensible ADPCM file is still refused");
	}

	//--------------------------------------------------------------------
	section ("4. Channels");
	//--------------------------------------------------------------------
	{
		// MONO IS DUPLICATED, not panned hard left.
		{
			WavSpec spec;
			spec.channels = 1;
			Bytes b; b.u16 (0x4000);
			spec.audio = b.data;

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "mono loads");
			check (buffer.frameCount == 1, "one frame");
			check (close (buffer.samples[0], buffer.samples[1], 1e-9),
			       "and is duplicated to both channels, not panned left");
			check (close (buffer.samples[0], 0.5, 1e-6), "at its own level");
		}

		// More than two takes the first two.
		{
			WavSpec spec;
			spec.channels = 4;
			Bytes b;
			b.u16 (0x4000); b.u16 (0xC000); b.u16 (0x2000); b.u16 (0x1000);
			spec.audio = b.data;

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded, "4-channel loads");
			check (buffer.frameCount == 1, "as one frame, not four");
			check (close (buffer.samples[0],  0.5, 1e-6), "taking channel 1 as left");
			check (close (buffer.samples[1], -0.5, 1e-6), "and channel 2 as right");
		}
	}

	//--------------------------------------------------------------------
	section ("5. Files that are not what they should be");
	//--------------------------------------------------------------------
	{
		SampleBuffer buffer;

		check (parseWav (nullptr, 0, buffer) == SampleStatus::NotWave, "no bytes at all");

		const std::vector<unsigned char> tiny = { 'R', 'I', 'F', 'F' };
		check (parse (tiny, buffer) == SampleStatus::NotWave, "four bytes is not a header");

		std::vector<unsigned char> notRiff = buildWav (WavSpec ());
		notRiff[0] = 'X';
		check (parse (notRiff, buffer) == SampleStatus::NotWave, "a file that is not RIFF");

		std::vector<unsigned char> notWave = buildWav (WavSpec ());
		notWave[8] = 'X';
		check (parse (notWave, buffer) == SampleStatus::NotWave, "RIFF but not WAVE");

		{
			WavSpec spec;
			spec.format = 0x0011;            // ADPCM
			spec.audio = pcm16Frames ();
			check (parse (buildWav (spec), buffer) == SampleStatus::UnsupportedFormat,
			       "a compressed format is refused, and says which problem it is");
		}

		{
			WavSpec spec;
			spec.omitData = true;
			check (parse (buildWav (spec), buffer) == SampleStatus::NoFrames,
			       "a wav with no data chunk");
		}

		{
			WavSpec spec;
			spec.audio.clear ();
			check (parse (buildWav (spec), buffer) == SampleStatus::NoFrames,
			       "a wav with an empty data chunk");
		}

		// A file cut short mid-download claims more than it has. The
		// reader must use what is there rather than reading past the end.
		{
			WavSpec spec;
			spec.audio = pcm16Frames ();
			spec.truncateData = true;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded,
			       "a truncated data chunk loads what is actually present");
			check (buffer.frameCount == 2, "which is the two frames that survived");
		}

		// The length cap, in seconds so it means the same at any rate.
		{
			WavSpec spec;
			spec.channels = 1;
			spec.bits = 8;
			spec.rate = 8000;
			spec.audio.assign (static_cast<std::size_t> (8000 * 61), 128);
			check (parse (buildWav (spec), buffer) == SampleStatus::TooLong,
			       "61 seconds is refused rather than silently truncated");

			spec.audio.assign (static_cast<std::size_t> (8000 * 59), 128);
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded,
			       "59 seconds is fine");
		}

		// Every refusal must leave the buffer EMPTY. A slot that failed
		// to load must not be handed the previous file's audio.
		{
			WavSpec spec;
			spec.format = 0x0011;
			spec.audio = pcm16Frames ();
			parse (buildWav (spec), buffer);
			check (buffer.empty (), "a refused file leaves an empty buffer behind");
		}
	}

	//--------------------------------------------------------------------
	section ("6. Chunks that are not fmt or data");
	//--------------------------------------------------------------------
	{
		// An ODD-LENGTH chunk is followed by a pad byte the size field
		// does not count. Missing it reads the next chunk's id one byte
		// late, and everything after the first odd chunk is lost - which
		// in practice means every file with metadata in it.
		WavSpec spec;
		spec.audio = pcm16Frames ();
		spec.oddChunkBefore = true;

		SampleBuffer buffer;
		check (parse (buildWav (spec), buffer) == SampleStatus::Loaded,
		       "an odd-length LIST chunk before the data is stepped over");
		check (buffer.frameCount == 2, "and the data chunk after it is still found");
		check (close (buffer.samples[0], -1.0, 1e-7), "with its samples intact");
	}

	//--------------------------------------------------------------------
	section ("7. Inferring a tempo from a length");
	//--------------------------------------------------------------------
	{
		// Two seconds. Every candidate beat count implies beats * 30 BPM,
		// so 3 beats is 90, 4 is 120 and 6 is 180 - all three inside the
		// window, which is exactly the ambiguity the reference resolves.
		int beats = 0;
		check (close (inferTempoFromLength (2.0, 120.0, &beats), 120.0, 1e-9),
		       "two seconds nearest 120 reads as four beats at 120");
		check (beats == 4, "and says so: four beats");

		check (close (inferTempoFromLength (2.0, 90.0, &beats), 90.0, 1e-9),
		       "the same file nearest 90 reads as three beats at 90");
		check (beats == 3, "three beats this time");

		// THE LOG-SPACE RULE, and its negative control. Against 150, the
		// candidates are 90, 120 and 180. Measured linearly 120 and 180
		// are both 30 away and 120 would win; measured as a ratio 180 is
		// 1.20x and 120 is 1.25x, so 180 wins. Tempo is a ratio.
		const double against150 = inferTempoFromLength (2.0, 150.0, &beats);
		check (close (against150, 180.0, 1e-9),
		       "against 150 the nearest candidate is 180, not 120");
		check (! close (against150, 120.0, 1e-9),
		       "NEGATIVE CONTROL: the distance is not measured linearly");

		// Outside the window nothing is guessed at. A tenth of a second
		// is 600 BPM at one beat and faster at every other candidate.
		check (close (inferTempoFromLength (0.1, 120.0, &beats), 0.0, 1e-12),
		       "a file too short for any plausible beat count gets no tempo");
		check (beats == 0, "and no beat count either");

		check (close (inferTempoFromLength (0.0, 120.0, nullptr), 0.0, 1e-12),
		       "a zero-length file gets no tempo");
		check (close (inferTempoFromLength (-1.0, 120.0, nullptr), 0.0, 1e-12),
		       "and neither does a negative one");

		// An absent reference is the constant, not zero - which would put
		// every candidate infinitely far away and pick the first.
		check (close (inferTempoFromLength (2.0, 0.0, nullptr), kReferenceBpm, 1e-9),
		       "no reference falls back to kReferenceBpm");
	}

	//--------------------------------------------------------------------
	section ("8. The tempo a loaded file comes with");
	//--------------------------------------------------------------------
	{
		// The inference, reached through the parser, and the reference
		// carried all the way from the caller.
		{
			SampleBuffer buffer;
			check (parse (buildWav (silenceOfSeconds (2.0)), buffer) == SampleStatus::Loaded,
			       "a two-second file loads");
			check (close (buffer.tempoBpm, 120.0, 1e-9), "and is taken to be 120");
			check (buffer.beats == 4,                    "four beats long");
			check (buffer.tempoSource == TempoSource::Inferred, "by inference");
			check (buffer.fittable (),                   "so it may be fitted");

			SampleBuffer against90;
			parse (buildWav (silenceOfSeconds (2.0)), against90, 90.0);
			check (close (against90.tempoBpm, 90.0, 1e-9),
			       "the same file in a 90 BPM project reads as 90");
			check (! close (against90.tempoBpm, 120.0, 1e-9),
			       "NEGATIVE CONTROL: the reference reaches the inference");
		}

		// An ACID chunk OUTRANKS the inference. Length alone would say
		// 120; the chunk says 100, and the chunk is a fact.
		{
			WavSpec spec = silenceOfSeconds (2.0);
			spec.hasAcid   = true;
			spec.acidBeats = 4;
			spec.acidTempo = 100.f;

			SampleBuffer buffer;
			check (parse (buildWav (spec), buffer) == SampleStatus::Loaded,
			       "a file with an acid chunk loads");
			check (close (buffer.tempoBpm, 100.0, 1e-5), "at the tempo the chunk states");
			check (buffer.tempoSource == TempoSource::AcidTempo, "and says where that came from");
			check (! close (buffer.tempoBpm, 120.0, 1e-5),
			       "NEGATIVE CONTROL: the chunk beat the length");
			check (! buffer.oneShot, "the one-shot flag is clear");
		}

		// A chunk with a beat count and no tempo is still arithmetic
		// rather than a guess: three beats in two seconds is 90.
		{
			WavSpec spec = silenceOfSeconds (2.0);
			spec.hasAcid   = true;
			spec.acidBeats = 3;
			spec.acidTempo = 0.f;

			SampleBuffer buffer;
			parse (buildWav (spec), buffer);
			check (close (buffer.tempoBpm, 90.0, 1e-9), "beats alone give 90");
			check (buffer.beats == 3, "and the chunk's beat count is kept");
			check (buffer.tempoSource == TempoSource::AcidBeats, "credited to the beat count");
			check (! close (buffer.tempoBpm, 120.0, 1e-9),
			       "NEGATIVE CONTROL: not the inferred 120");
		}

		// A ONE-SHOT gets no tempo at all, however suggestive its length.
		{
			WavSpec spec = silenceOfSeconds (2.0);
			spec.hasAcid   = true;
			spec.acidFlags = 0x01;
			spec.acidBeats = 0;
			spec.acidTempo = 0.f;

			SampleBuffer buffer;
			parse (buildWav (spec), buffer);
			check (buffer.oneShot,                              "the one-shot flag is read");
			check (close (buffer.tempoBpm, 0.0, 1e-12),         "and it gets no tempo");
			check (buffer.tempoSource == TempoSource::None,     "from nowhere");
			check (! buffer.fittable (),                        "so it is never fitted");

			// The control: the identical file without the flag WOULD have
			// been given one. The flag is doing the work, not the length.
			WavSpec plain = silenceOfSeconds (2.0);
			SampleBuffer other;
			parse (buildWav (plain), other);
			check (other.fittable (),
			       "NEGATIVE CONTROL: the same length without the flag is fitted");
		}

		// A library writing nonsense into the tempo field is not followed.
		{
			WavSpec spec = silenceOfSeconds (2.0);
			spec.hasAcid   = true;
			spec.acidBeats = 0;
			spec.acidTempo = 6000.f;

			SampleBuffer buffer;
			parse (buildWav (spec), buffer);
			check (! close (buffer.tempoBpm, 6000.0, 1e-3), "6000 BPM is not believed");
			check (buffer.tempoSource == TempoSource::Inferred,
			       "and the length is used instead");
		}

		// A file whose length suits no beat count is left alone, rather
		// than stretched on the strength of a bad guess.
		{
			SampleBuffer buffer;
			parse (buildWav (silenceOfSeconds (0.1)), buffer);
			check (buffer.frameCount == 100,                 "a tenth of a second loads");
			check (close (buffer.tempoBpm, 0.0, 1e-12),      "with no tempo");
			check (buffer.tempoSource == TempoSource::None,  "and nothing claimed about it");
			check (! buffer.fittable (),                     "so it plays as it arrived");
		}

		// The acid chunk is ODD-LENGTH-adjacent territory: prove it is
		// still found when it follows the data chunk rather than
		// preceding it, since chunk order is not fixed by the format.
		{
			SampleBuffer buffer;
			WavSpec spec = silenceOfSeconds (2.0);
			spec.hasAcid        = true;
			spec.acidTempo      = 100.f;
			spec.oddChunkBefore = true;
			parse (buildWav (spec), buffer);
			check (close (buffer.tempoBpm, 100.0, 1e-5),
			       "an acid chunk after an odd-length chunk is still read");
		}
	}

	//--------------------------------------------------------------------
	section ("9. The status texts");
	//--------------------------------------------------------------------
	{
		const SampleStatus all[] = {
			SampleStatus::Empty, SampleStatus::Loaded, SampleStatus::Unreadable,
			SampleStatus::NotWave, SampleStatus::UnsupportedFormat,
			SampleStatus::TooLong, SampleStatus::NoFrames };

		bool allNamed = true;
		for (SampleStatus status : all)
		{
			const char* text = sampleStatusText (status);
			// The panel puts these in a tooltip, so a null or an
			// "unknown" would be a slot that will not play and will not
			// say why.
			allNamed &= (text != nullptr && text[0] != '\0'
			             && std::string (text) != "unknown");
		}
		check (allNamed, "every status has a line the tooltip can show");

		const TempoSource sources[] = {
			TempoSource::None, TempoSource::AcidTempo,
			TempoSource::AcidBeats, TempoSource::Inferred };

		bool allSourcesNamed = true;
		for (TempoSource source : sources)
		{
			const char* text = tempoSourceText (source);
			// The panel shows this beside the detected tempo, and "how do
			// we know" is the whole value of showing it.
			allSourcesNamed &= (text != nullptr && text[0] != '\0'
			                    && std::string (text) != "unknown");
		}
		check (allSourcesNamed, "and so does every tempo source");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
