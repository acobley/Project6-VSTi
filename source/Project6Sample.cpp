//------------------------------------------------------------------------
// Project6 - the WAV reader, implementation
//
// SDK-free. Every multi-byte value is assembled from bytes rather than
// cast from the buffer: WAV is little-endian and a struct cast would be
// both unaligned and wrong on a big-endian machine, which is the kind of
// bug that survives every test you run on the machine you wrote it on.
//------------------------------------------------------------------------

#include "Project6Sample.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace Project6 {

namespace {

//------------------------------------------------------------------------
// Little-endian readers. Bounds are the caller's business; every call
// below is guarded by an explicit size check first.
//------------------------------------------------------------------------
uint16_t readU16 (const unsigned char* p)
{
	return static_cast<uint16_t> (p[0] | (p[1] << 8));
}

uint32_t readU32 (const unsigned char* p)
{
	return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8)
	       | (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24);
}

bool tagIs (const unsigned char* p, const char* tag)
{
	return p[0] == static_cast<unsigned char> (tag[0])
	       && p[1] == static_cast<unsigned char> (tag[1])
	       && p[2] == static_cast<unsigned char> (tag[2])
	       && p[3] == static_cast<unsigned char> (tag[3]);
}

/** The two format codes worth knowing, and the wrapper that hides them. */
constexpr uint16_t kFormatPcm        = 0x0001;
constexpr uint16_t kFormatFloat      = 0x0003;
constexpr uint16_t kFormatExtensible = 0xFFFE;

//------------------------------------------------------------------------
/** One sample, converted to -1..1.

    The divisors are the NEGATIVE full scale, which is the convention: a
    16-bit file's -32768 becomes exactly -1 and its +32767 becomes
    0.999969. Dividing by 32767 instead would let -32768 reach -1.000031
    and clip a file that was already at full scale. */
float decodeSample (const unsigned char* p, uint16_t format, uint16_t bits)
{
	if (format == kFormatFloat)
	{
		if (bits == 32)
		{
			uint32_t bits32 = readU32 (p);
			float value = 0.f;
			std::memcpy (&value, &bits32, sizeof (value));
			return value;
		}

		// 64-bit float. Assembled the same way, for the same reason.
		uint64_t bits64 = 0;
		for (int i = 7; i >= 0; --i)
			bits64 = (bits64 << 8) | p[i];
		double value = 0.0;
		std::memcpy (&value, &bits64, sizeof (value));
		return static_cast<float> (value);
	}

	switch (bits)
	{
		case 8:
			// 8-bit wav is UNSIGNED, alone among the PCM depths. Silence
			// is 128, not 0, and treating it as signed turns a quiet file
			// into a loud square wave.
			return (static_cast<int> (p[0]) - 128) / 128.f;

		case 16:
			return static_cast<int16_t> (readU16 (p)) / 32768.f;

		case 24:
		{
			// Sign-extend three bytes into an int32.
			int32_t value = static_cast<int32_t> (
				(static_cast<uint32_t> (p[0]) << 8) | (static_cast<uint32_t> (p[1]) << 16)
				| (static_cast<uint32_t> (p[2]) << 24));
			return (value >> 8) / 8388608.f;
		}

		case 32:
			return static_cast<int32_t> (readU32 (p)) / 2147483648.f;

		default:
			return 0.f;
	}
}

} // namespace

//------------------------------------------------------------------------
const char* sampleStatusText (SampleStatus status)
{
	switch (status)
	{
		case SampleStatus::Empty:             return "empty";
		case SampleStatus::Loaded:            return "loaded";
		case SampleStatus::Unreadable:        return "could not be read";
		case SampleStatus::NotWave:           return "not a WAV file";
		case SampleStatus::UnsupportedFormat: return "unsupported WAV format";
		case SampleStatus::TooLong:           return "longer than 60 seconds";
		case SampleStatus::NoFrames:          return "no audio in the file";
	}
	return "unknown";
}

//------------------------------------------------------------------------
SampleStatus parseWav (const unsigned char* data, std::size_t size, SampleBuffer& out)
{
	out = SampleBuffer ();

	// RIFF....WAVE is twelve bytes before any chunk starts.
	if (data == nullptr || size < 12)
		return SampleStatus::NotWave;

	// RIFX is the big-endian variant and is not decoded here. Saying
	// NotWave for it is honest: this reader does not read that file.
	if (!tagIs (data, "RIFF") || !tagIs (data + 8, "WAVE"))
		return SampleStatus::NotWave;

	bool     haveFormat = false;
	uint16_t format     = 0;
	uint16_t channels   = 0;
	uint32_t rate       = 0;
	uint16_t bits       = 0;
	uint16_t blockAlign = 0;

	const unsigned char* audio = nullptr;
	std::size_t audioBytes = 0;

	std::size_t offset = 12;
	while (offset + 8 <= size)
	{
		const unsigned char* id = data + offset;
		const uint32_t chunkSize = readU32 (data + offset + 4);
		const std::size_t body = offset + 8;

		// A chunk claiming more bytes than the file holds is a TRUNCATED
		// FILE, not a reason to read past the end. What has been found so
		// far is still used, so a file cut short after its data chunk
		// still plays.
		const std::size_t available = (body < size) ? (size - body) : 0;
		const std::size_t usable = std::min (static_cast<std::size_t> (chunkSize), available);

		if (tagIs (id, "fmt ") && usable >= 16)
		{
			format     = readU16 (data + body);
			channels   = readU16 (data + body + 2);
			rate       = readU32 (data + body + 4);
			blockAlign = readU16 (data + body + 12);
			bits       = readU16 (data + body + 14);

			// WAVE_FORMAT_EXTENSIBLE is what most 24-bit files really
			// are: the real format code is the first two bytes of the
			// SubFormat GUID, 24 bytes into the extension.
			if (format == kFormatExtensible && usable >= 40)
				format = readU16 (data + body + 24);

			haveFormat = true;
		}
		else if (tagIs (id, "data"))
		{
			audio = data + body;
			audioBytes = usable;
		}

		// Chunks are WORD-ALIGNED: an odd-length chunk is followed by a
		// pad byte that the size field does not count. Missing this reads
		// the next chunk's id one byte late and loses everything after
		// the first odd chunk - and metadata chunks are odd all the time.
		std::size_t advance = 8 + static_cast<std::size_t> (chunkSize);
		if ((chunkSize & 1u) != 0u)
			++advance;

		if (advance <= 8)               // a zero-size chunk still moves on
			advance = 8;
		offset += advance;
	}

	if (!haveFormat || channels == 0 || rate == 0)
		return SampleStatus::NotWave;

	const bool decodablePcm   = (format == kFormatPcm)
	                            && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
	const bool decodableFloat = (format == kFormatFloat) && (bits == 32 || bits == 64);
	if (!decodablePcm && !decodableFloat)
		return SampleStatus::UnsupportedFormat;

	const std::size_t bytesPerSample = bits / 8u;
	const std::size_t frameBytes     = bytesPerSample * channels;
	if (frameBytes == 0)
		return SampleStatus::UnsupportedFormat;

	// blockAlign is advisory: plenty of files get it wrong, and the
	// channel count and bit depth are what the samples are actually laid
	// out by. It is read for completeness and deliberately not trusted.
	(void) blockAlign;

	if (audio == nullptr || audioBytes < frameBytes)
		return SampleStatus::NoFrames;

	const std::size_t frames = audioBytes / frameBytes;

	// The cap is in SECONDS, not frames, so it means the same thing at
	// 44.1 k and at 192 k.
	if (static_cast<double> (frames) / static_cast<double> (rate) > kMaxSampleSeconds)
		return SampleStatus::TooLong;

	out.frameCount = static_cast<int> (frames);
	out.sourceRate = static_cast<double> (rate);
	out.samples.assign (frames * kSampleChannels, 0.f);

	float peak = 0.f;
	for (std::size_t frame = 0; frame < frames; ++frame)
	{
		const unsigned char* source = audio + frame * frameBytes;

		// MONO IS DUPLICATED, not panned left. More than two channels
		// takes the first two and ignores the rest, which is what a
		// stereo slot can honestly do with a surround file.
		const float left  = decodeSample (source, format, bits);
		const float right = (channels >= 2)
		                        ? decodeSample (source + bytesPerSample, format, bits)
		                        : left;

		out.samples[frame * kSampleChannels]     = left;
		out.samples[frame * kSampleChannels + 1] = right;

		peak = std::max (peak, std::max (std::fabs (left), std::fabs (right)));
	}

	out.peak = peak;
	return SampleStatus::Loaded;
}

//------------------------------------------------------------------------
SampleStatus loadWavFile (const std::string& path, SampleBuffer& out)
{
	out = SampleBuffer ();

	if (path.empty ())
		return SampleStatus::Empty;

	std::ifstream file (path, std::ios::binary);
	if (!file)
		return SampleStatus::Unreadable;

	file.seekg (0, std::ios::end);
	const std::streamoff length = file.tellg ();
	file.seekg (0, std::ios::beg);

	if (length <= 0)
		return SampleStatus::Unreadable;

	// A hard ceiling on what is read into memory at all, before any
	// header is trusted. kMaxSampleSeconds is the real limit; this is
	// only here so that a pathological "file" cannot ask for an
	// arbitrary allocation on the strength of its own size.
	constexpr std::streamoff kMaxFileBytes = 512ll * 1024ll * 1024ll;
	if (length > kMaxFileBytes)
		return SampleStatus::TooLong;

	std::vector<unsigned char> bytes (static_cast<std::size_t> (length));
	if (!file.read (reinterpret_cast<char*> (bytes.data ()), length))
		return SampleStatus::Unreadable;

	return parseWav (bytes.data (), bytes.size (), out);
}

//------------------------------------------------------------------------
} // namespace Project6
