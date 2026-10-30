//------------------------------------------------------------------------
// Project6 - the tempo fit, implementation
//
// SDK-free. Every length in here is written in SECONDS and converted in
// setSampleRate, because a hop of "1500 samples" is 34 ms at 44.1 k and
// 7.8 ms at 192 k, and a stretcher whose grain size quietly quarters when
// the host changes rate is a stretcher that sounds different on someone
// else's machine.
//------------------------------------------------------------------------

#include "Project6Stretch.h"
#include "Project6Sample.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Project6 {

namespace {

//------------------------------------------------------------------------
// The grain geometry.
//
// A hop of about 34 ms with a 12 ms overlap is the standard WSOLA
// starting point and is what these numbers are: long enough that the
// splices are sparse, short enough that the timing correction happens
// often enough to track a fit of a few percent without audible drift.
//
// The search window is +/- 6 ms, which covers a full period of anything
// down to about 83 Hz. Below that the correlation cannot see a whole
// cycle and the splice is chosen on partial evidence - which is why bass
// material is the first thing to sound wrong in any stretcher, this one
// included, and why varispeed is the default.
//------------------------------------------------------------------------
constexpr double kHopSeconds     = 0.034;
constexpr double kOverlapSeconds = 0.012;
constexpr double kSearchSeconds  = 0.006;

/** How many multiplies the correlation is allowed, per candidate offset,
    and how many candidates it may try. FIXED COUNTS, not fixed strides:
    the cost of a hop is then the same at 44.1 k and at 192 k, instead of
    quadrupling with the rate on a thread that has a deadline. */
constexpr int kCorrelationTaps = 64;
constexpr int kSearchSteps     = 128;

/** One channel of `source` at an integer frame, wrapped.

    Mono, because the correlation is looking for where the waveform
    repeats and both channels of a stereo loop repeat in the same place.
    Summing them also halves the work. */
inline double monoAt (const float* source, int frames, int index)
{
	// A true modulo, so a negative index reads from the end of the loop
	// rather than off the front of the buffer.
	int at = index % frames;
	if (at < 0)
		at += frames;

	const std::size_t base = static_cast<std::size_t> (at) * kSampleChannels;
	return static_cast<double> (source[base]) + static_cast<double> (source[base + 1]);
}

//------------------------------------------------------------------------
// THE BAND-LIMITED READ
//
// Linear interpolation is fine at a step of 1 or below, and does NOTHING
// against aliasing above it. A read head advancing r source frames per
// output frame folds everything in the file above Nyquist / r straight
// back into the audible band, nearly unattenuated: a 2x Varispeed fold
// (60 -> 120 BPM) puts the hats and cymbals above 12 kHz back down as
// inharmonic junk at -0.5 dB. AUDIT-denormals-aliasing.md has the numbers.
//
// Above a step of 1 the read is therefore a Kaiser-windowed sinc whose
// cutoff follows the step: a low-pass at kSincCutoff x the OUTPUT's
// Nyquist, so whatever cannot exist after the read is removed before it.
// Measured (kSincZeroCrossings 6, beta 7, cutoff 0.94; StretchTests 9):
// what would fold below 16 kHz comes out at about -70 dB or lower,
// against -0.5 to -7 dB linear; 19 kHz output is down about 1 dB.
//
// AT OR BELOW A STEP OF 1 NOTHING CHANGES: the linear read is kept, so a
// pad at its own rate and tempo is still bit-identical to its file.
//------------------------------------------------------------------------
constexpr int    kSincZeroCrossings = 6;      ///< either side, at the cutoff
constexpr double kSincCutoff        = 0.94;   ///< of the output's Nyquist
constexpr double kSincBeta          = 7.0;    ///< Kaiser
constexpr int    kSincResolution    = 512;    ///< table points per zero crossing
constexpr int    kSincMaxHalfWidth  = 32;     ///< source frames either side, at most
constexpr int    kSincMaxTaps       = 2 * kSincMaxHalfWidth + 2;

/** The prototype kernel sinc(u) x kaiser(u / kSincZeroCrossings) for
    u = 0 .. kSincZeroCrossings, built once. Symmetric, so one side. */
struct SincTable
{
	float v[kSincZeroCrossings * kSincResolution + 2];

	SincTable ()
	{
		const double pi = 3.14159265358979323846;
		auto besselI0 = [] (double x)
		{
			double sum = 1.0, term = 1.0;
			for (int k = 1; k < 40; ++k)
			{
				term *= (x / (2.0 * k)) * (x / (2.0 * k));
				sum += term;
			}
			return sum;
		};
		const double norm = besselI0 (kSincBeta);
		const int n = kSincZeroCrossings * kSincResolution;
		for (int i = 0; i <= n; ++i)
		{
			const double u = static_cast<double> (i) / kSincResolution;
			const double x = u / kSincZeroCrossings;
			const double window = besselI0 (kSincBeta * std::sqrt (std::max (0.0, 1.0 - x * x))) / norm;
			const double sinc = (i == 0) ? 1.0 : std::sin (pi * u) / (pi * u);
			v[i] = static_cast<float> (sinc * window);
		}
		v[n + 1] = 0.f;                 // so the interpolation at the edge reads a zero
	}
};

const SincTable& sincTable ()
{
	// A function-local static: built once, thread-safely. setSampleRate
	// touches it first, off the audio thread, so the audio thread never
	// pays for the construction.
	static const SincTable table;
	return table;
}

constexpr int    kSincPhases        = 128;    ///< bank rows per source frame
constexpr double kSincRebuildTolerance = 0.005;  ///< relative ratio change before a rebuild

using SincBank = TimeStretcher::SincBank;

/** The bank for `ratio`: kSincPhases + 1 rows, row p the kernel for a
    read position p / kSincPhases past an integer frame, tap j at frame
    offset j - lead. The cutoff is kSincCutoff x the output's Nyquist. */
void buildBank (SincBank& bank, double ratio)
{
	const float* table = sincTable ().v;
	const double fc   = kSincCutoff / ratio;
	const double half = std::min (kSincZeroCrossings / fc, static_cast<double> (kSincMaxHalfWidth));
	const int lead = static_cast<int> (std::ceil (half));
	const int taps = std::min (2 * lead + 1, kSincMaxTaps);
	const double edge = static_cast<double> (kSincZeroCrossings * kSincResolution);

	bank.ratio = ratio;
	bank.taps  = taps;
	bank.lead  = lead;

	for (int p = 0; p <= kSincPhases; ++p)
	{
		float* row = bank.weights.data () + static_cast<std::size_t> (p) * kSincMaxTaps;
		const double frac = static_cast<double> (p) / kSincPhases;
		double sum = 0.0;
		for (int j = 0; j < taps; ++j)
		{
			const double t  = static_cast<double> (j - lead) - frac;
			if (std::fabs (t) > half)
			{
				row[j] = 0.f;           // past a capped half-width: cut short
				continue;
			}
			const double ax = std::fabs (t * fc) * kSincResolution;
			double w = 0.0;
			if (ax < edge)
			{
				const int i = static_cast<int> (ax);
				w = table[i] + (table[i + 1] - table[i]) * (ax - static_cast<double> (i));
			}
			row[j] = static_cast<float> (w);
			sum += w;
		}
		// Unity DC gain for every phase, over every tap - read or not.
		const float norm = (sum > 0.0) ? static_cast<float> (1.0 / sum) : 0.f;
		for (int j = 0; j < taps; ++j)
			row[j] *= norm;
	}
}

/** One stereo frame at `position`, low-passed for a read advancing
    `ratio` (> 1) source frames per output frame, from the bank.

    Taps past either end of the file WRAP when the pad loops - the same
    rule as the linear read's second tap, so a loop point does not click -
    and read as SILENCE when it does not, so a one-shot's attack is never
    smeared with its own tail.

    THE COST is the taps: about 2 x kSincZeroCrossings x ratio /
    kSincCutoff of them - 15 at 1.2x, 51 at 4x - against linear's 2. A
    polyphase bank (two rows interpolated, applied to a contiguous run of
    the file) keeps that to a few times linear's cost; working each
    weight out from the table per tap cost three times as much again.
    Beyond kSincMaxHalfWidth (a step of about 5) the kernel is cut short
    rather than allowed to grow: that is a fit the tempo detection got
    wrong, and it should not cost the session a core. */
inline void readBandLimited (SincBank& bank, const float* source, int frames, double position,
                             double ratio, bool looping, double& left, double& right)
{
	if (bank.weights.empty ())
		bank.weights.resize (static_cast<std::size_t> (kSincPhases + 1) * kSincMaxTaps);  // only if setSampleRate never ran
	if (std::fabs (ratio - bank.ratio) > kSincRebuildTolerance * ratio)
		buildBank (bank, ratio);

	const int whole = static_cast<int> (std::floor (position));
	const double phase = (position - static_cast<double> (whole)) * kSincPhases;
	int p = static_cast<int> (phase);
	if (p >= kSincPhases)
		p = kSincPhases - 1;
	const float f = static_cast<float> (phase - static_cast<double> (p));

	const float* row0 = bank.weights.data () + static_cast<std::size_t> (p) * kSincMaxTaps;
	const float* row1 = row0 + kSincMaxTaps;
	const int first = whole - bank.lead;
	const int taps  = bank.taps;

	float l = 0.f, r = 0.f;
	if (first >= 0 && first + taps <= frames)
	{
		// The usual case: every tap inside the file, one contiguous run.
		const float* src = source + static_cast<std::size_t> (first) * kSampleChannels;
		for (int j = 0; j < taps; ++j)
		{
			const float w = row0[j] + (row1[j] - row0[j]) * f;
			l += w * src[2 * j];
			r += w * src[2 * j + 1];
		}
	}
	else
	{
		for (int j = 0; j < taps; ++j)
		{
			int at = first + j;
			if (at < 0 || at >= frames)
			{
				if (!looping)
					continue;           // outside a one-shot: silence
				at %= frames;
				if (at < 0)
					at += frames;
			}
			const float w = row0[j] + (row1[j] - row0[j]) * f;
			const std::size_t base = static_cast<std::size_t> (at) * kSampleChannels;
			l += w * source[base];
			r += w * source[base + 1];
		}
	}
	left  = l;
	right = r;
}

/** Interpolated stereo at a fractional position, wrapped, for a read
    advancing `ratio` source frames per output frame.

    At a ratio of 1 or below this is the linear read the DSP has always
    done. THE SECOND TAP WRAPS TO FRAME 0 - the reason a loop point does
    not click. Above 1 it is readBandLimited. */
inline void readAt (SincBank& bank, const float* source, int frames, double position, double ratio,
                    bool looping, double& left, double& right)
{
	if (ratio > 1.0 + 1e-9)
	{
		readBandLimited (bank, source, frames, position, ratio, looping, left, right);
		return;
	}

	int first = static_cast<int> (position);
	if (first < 0 || first >= frames)
		first = 0;                      // belt and braces against a rounding edge
	const double fraction = position - static_cast<double> (first);

	const int second = (first + 1 < frames) ? first + 1 : 0;

	const std::size_t a = static_cast<std::size_t> (first) * kSampleChannels;
	const std::size_t b = static_cast<std::size_t> (second) * kSampleChannels;

	left  = source[a]     + (source[b]     - source[a])     * fraction;
	right = source[a + 1] + (source[b + 1] - source[a + 1]) * fraction;
}

/** Put a position back inside the file. */
inline double wrapPosition (double position, int frames)
{
	const double length = static_cast<double> (frames);
	if (position >= length || position < 0.0)
	{
		// fmod rather than a subtraction: a short file at a large ratio
		// can pass the end several times in one output sample.
		position = std::fmod (position, length);
		if (position < 0.0)
			position += length;
	}
	return position;
}

} // namespace

//------------------------------------------------------------------------
const char* fitModeName (FitMode mode)
{
	switch (mode)
	{
		case FitMode::Off:            return "Off";
		case FitMode::Varispeed:      return "Varispeed";
		case FitMode::PitchPreserved: return "Keep pitch";
	}
	return "Off";
}

//------------------------------------------------------------------------
const char* fitModeShortName (FitMode mode)
{
	switch (mode)
	{
		case FitMode::Off:            return "off";
		case FitMode::Varispeed:      return "spd";
		case FitMode::PitchPreserved: return "pch";
	}
	return "off";
}

//------------------------------------------------------------------------
FitMode fitModeFromIndex (int index)
{
	switch (index)
	{
		case 1:  return FitMode::Varispeed;
		case 2:  return FitMode::PitchPreserved;
		default: return FitMode::Off;
	}
}

//------------------------------------------------------------------------
int indexOfFitMode (FitMode mode)
{
	switch (mode)
	{
		case FitMode::Varispeed:      return 1;
		case FitMode::PitchPreserved: return 2;
		case FitMode::Off:            return 0;
	}
	return 0;
}

//------------------------------------------------------------------------
double fitSpeed (double fileBpm, double projectBpm)
{
	// EITHER ONE UNKNOWN MEANS NO FIT. A file we could not read a tempo
	// from, or a host that did not send a tempo, plays exactly as it
	// arrived - which is the behaviour that can be heard and corrected,
	// rather than a stretch by some assumed ratio that cannot.
	if (!(fileBpm > 0.0) || !(projectBpm > 0.0))
		return 1.0;

	const double speed = projectBpm / fileBpm;
	return std::min (kMaxFitSpeed, std::max (kMinFitSpeed, speed));
}

//------------------------------------------------------------------------
void TimeStretcher::setSampleRate (double sampleRate)
{
	if (!(sampleRate > 0.0))
		return;

	mHop     = std::max (1, static_cast<int> (kHopSeconds * sampleRate));
	mOverlap = std::max (1, static_cast<int> (kOverlapSeconds * sampleRate));
	mSearch  = std::max (1, static_cast<int> (kSearchSeconds * sampleRate));

	// Bounded work per hop, whatever the rate. Both strides are at least
	// one, so a very low rate simply looks at every sample.
	mCorrStep = std::max (1, mOverlap / kCorrelationTaps);

	// The band-limited read's table and bank memory, here and off the
	// audio thread, so the first fast read allocates and builds nothing
	// but the bank's rows.
	sincTable ();
	mBank.weights.assign (static_cast<std::size_t> (kSincPhases + 1) * kSincMaxTaps, 0.f);
	mBank.ratio = 0.0;
	mSeekStep = std::max (1, mSearch / kSearchSteps);

	reset ();
}

//------------------------------------------------------------------------
void TimeStretcher::reset ()
{
	mIdeal = 0.0;
	mRead  = 0.0;
	mPrev  = 0.0;
	mFade  = 0;
	mWrapped = false;

	// A HOP'S WORTH OF CREDIT AT THE START, so the first splice happens
	// after a hop of audio rather than on the first sample - where there
	// is no drift to correct and nothing behind the playhead to fade out
	// of.
	mSinceHop = 0;
}

//------------------------------------------------------------------------
void TimeStretcher::setPosition (double position)
{
	mIdeal = position;
	mRead  = position;
	mPrev  = position;
	mFade  = 0;
	mSinceHop = 0;
	mWrapped = false;
}

//------------------------------------------------------------------------
bool TimeStretcher::takeWrapped ()
{
	const bool wrapped = mWrapped;
	mWrapped = false;
	return wrapped;
}

//------------------------------------------------------------------------
bool TimeStretcher::canSplice (int frames) const
{
	// A splice needs a fade to happen over and a window either side of it
	// to search in. A file shorter than that has nowhere to put one, so
	// the mode falls back to varispeed rather than splicing into itself.
	return frames > 2 * (mOverlap + mSearch);
}

//------------------------------------------------------------------------
double TimeStretcher::findSplice (const float* source, int frames, double drift) const
{
	// The reference is what the OUTGOING head was about to play. The best
	// splice is the offset whose audio continues it most convincingly.
	const int base = static_cast<int> (mRead);

	double reference[kCorrelationTaps];
	int taps = 0;
	for (int k = 0; k < mOverlap && taps < kCorrelationTaps; k += mCorrStep, ++taps)
		reference[taps] = monoAt (source, frames, base + k);

	if (taps == 0)
		return drift;

	double bestScore  = 0.0;
	double bestOffset = drift;
	bool   haveBest   = false;

	for (int offset = -mSearch; offset <= mSearch; offset += mSeekStep)
	{
		const int candidate = static_cast<int> (mRead + drift) + offset;

		double dot = 0.0;
		double energy = 0.0;
		for (int t = 0; t < taps; ++t)
		{
			const double value = monoAt (source, frames, candidate + t * mCorrStep);
			dot    += reference[t] * value;
			energy += value * value;
		}

		// NORMALISED by the candidate's own energy, or every splice would
		// land on the loudest moment in the search window instead of the
		// best-matching one - which is how a stretcher turns a drum loop
		// into a stutter on the kick.
		const double score = dot / std::sqrt (energy + 1e-12);

		if (!haveBest || score > bestScore)
		{
			bestScore  = score;
			bestOffset = drift + offset;
			haveBest   = true;
		}
	}

	return bestOffset;
}

//------------------------------------------------------------------------
void TimeStretcher::next (const float* source, int frames, double rateStep, double speed,
                          FitMode mode, double& left, double& right)
{
	left  = 0.0;
	right = 0.0;

	if (source == nullptr || frames <= 0)
		return;

	// Off means the tempo is ignored, which is exactly speed 1.
	const double fit = (mode == FitMode::Off) ? 1.0 : speed;

	const bool splicing = (mode == FitMode::PitchPreserved)
	                      && canSplice (frames)
	                      && std::fabs (fit - 1.0) > 1e-9;

	if (!splicing)
	{
		// VARISPEED, and the fallbacks that land on it. One head, and the
		// read is the one the DSP has always done - which is why a pad at
		// speed 1.0 is still bit-identical to its file.
		readAt (mBank, source, frames, mIdeal, std::fabs (rateStep * fit), mLooping, left, right);

		const double before = mIdeal;
		mIdeal = wrapPosition (mIdeal + rateStep * fit, frames);
		if (mIdeal < before)
			mWrapped = true;

		mRead  = mIdeal;
		mPrev  = mIdeal;
		mFade  = 0;
		mSinceHop = 0;
		return;
	}

	//--------------------------------------------------------------------
	// PITCH-PRESERVED. Splice first, then read, then advance - so a
	// splice decided this sample is heard from this sample.
	//--------------------------------------------------------------------
	if (mFade <= 0 && mSinceHop >= mHop)
	{
		// The drift, as the SHORTEST WAY ROUND a loop: a read head just
		// past the loop point and an ideal head just before it are a few
		// samples apart, not a whole file apart.
		double drift = mIdeal - mRead;
		const double length = static_cast<double> (frames);
		drift = std::fmod (drift, length);
		if (drift > length * 0.5)
			drift -= length;
		else if (drift < -length * 0.5)
			drift += length;

		mPrev = mRead;
		mRead = wrapPosition (mRead + findSplice (source, frames, drift), frames);
		mFade = mOverlap;
		mSinceHop = 0;
	}

	double newLeft = 0.0, newRight = 0.0;
	readAt (mBank, source, frames, mRead, std::fabs (rateStep), mLooping, newLeft, newRight);

	if (mFade > 0)
	{
		double oldLeft = 0.0, oldRight = 0.0;
		readAt (mBank, source, frames, mPrev, std::fabs (rateStep), mLooping, oldLeft, oldRight);

		// LINEAR, not equal-power. The two grains were chosen to be in
		// phase with each other, so their sum holds its level through the
		// fade; an equal-power curve, which is for uncorrelated signals,
		// would bulge in the middle of every join.
		const double mix = 1.0 - (static_cast<double> (mFade) / static_cast<double> (mOverlap));

		newLeft  = oldLeft  + (newLeft  - oldLeft)  * mix;
		newRight = oldRight + (newRight - oldRight) * mix;

		mPrev = wrapPosition (mPrev + rateStep, frames);
		--mFade;
	}

	left  = newLeft;
	right = newRight;

	// THE READ HEAD MOVES AT THE FILE'S OWN RATE - that is the whole of
	// why the pitch survives - while the ideal head moves at the tempo.
	mRead  = wrapPosition (mRead + rateStep, frames);

	const double before = mIdeal;
	mIdeal = wrapPosition (mIdeal + rateStep * fit, frames);
	if (mIdeal < before)
		mWrapped = true;

	++mSinceHop;
}

//------------------------------------------------------------------------
} // namespace Project6
