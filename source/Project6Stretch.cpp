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

/** Interpolated stereo at a fractional position, wrapped.

    THE SECOND TAP WRAPS TO FRAME 0 - the same rule the DSP has always
    used, and the reason a loop point does not click. */
inline void readAt (const float* source, int frames, double position,
                    double& left, double& right)
{
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
		readAt (source, frames, mIdeal, left, right);

		mIdeal = wrapPosition (mIdeal + rateStep * fit, frames);
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
	readAt (source, frames, mRead, newLeft, newRight);

	if (mFade > 0)
	{
		double oldLeft = 0.0, oldRight = 0.0;
		readAt (source, frames, mPrev, oldLeft, oldRight);

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
	mIdeal = wrapPosition (mIdeal + rateStep * fit, frames);
	++mSinceHop;
}

//------------------------------------------------------------------------
} // namespace Project6
