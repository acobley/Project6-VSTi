//------------------------------------------------------------------------
// Project6 - the bar clock, implementation
//------------------------------------------------------------------------

#include "Project6Transport.h"

#include <algorithm>
#include <cmath>

namespace Project6 {

namespace {

/** A tolerance in quarter notes. A bar line half a millionth of a beat
    before the block it belongs to is the same bar line, and floating
    point arithmetic on a project position measured in hundreds of beats
    will produce exactly that. */
constexpr double kEpsilon = 1.0e-9;

} // namespace

//------------------------------------------------------------------------
double TransportInfo::barLength () const
{
	if (sigNumerator <= 0 || sigDenominator <= 0)
		return 0.0;

	// A bar is `numerator` notes of `1/denominator` each, and the unit
	// here is the QUARTER note - so 4/4 is 4, 3/4 is 3, 6/8 is 3 and 7/8
	// is 3.5. Assuming four would put every bar line in the wrong place
	// in half the music anyone writes.
	return static_cast<double> (sigNumerator) * 4.0 / static_cast<double> (sigDenominator);
}

//------------------------------------------------------------------------
double barPhase (const TransportInfo& info)
{
	const double bar = info.barLength ();
	if (!(bar > 0.0))
		return 0.0;

	const double into = info.ppq - std::floor (info.ppq / bar) * bar;
	return std::min (1.0, std::max (0.0, into / bar));
}

//------------------------------------------------------------------------
long long barNumber (const TransportInfo& info)
{
	const double bar = info.barLength ();
	if (!(bar > 0.0))
		return 1;

	return static_cast<long long> (std::floor (info.ppq / bar)) + 1;
}

//------------------------------------------------------------------------
void BarClock::reset ()
{
	mLastFiredBar     = kNever;
	mPreviousStartPpq = kNever;
	mHavePrevious     = false;
}

//------------------------------------------------------------------------
int BarClock::barLinesInBlock (const TransportInfo& info, int numSamples, double sampleRate,
                               int* offsets, int maxOffsets)
{
	if (offsets == nullptr || maxOffsets <= 0)
		return 0;

	const double bar = info.barLength ();

	if (!info.playing || !info.musical || numSamples <= 0 || sampleRate <= 0.0
	    || !(bar > 0.0) || !(info.tempoBpm > 0.0))
	{
		// Nothing can be located, so nothing may be remembered either: the
		// next block that CAN locate a bar must not be measured against a
		// position from before the gap.
		mHavePrevious = false;
		return 0;
	}

	const double ppqPerSample = info.tempoBpm / 60.0 / sampleRate;
	if (!(ppqPerSample > 0.0))
	{
		mHavePrevious = false;
		return 0;
	}

	const double start = info.ppq;
	const double end   = start + static_cast<double> (numSamples) * ppqPerSample;

	// A JUMP BACKWARDS is a locate or a cycle wrapping, and it clears the
	// guard. Without this a one-bar cycle plays its bar line once and then
	// never again, because the same line keeps arriving and keeps being
	// recognised as one already fired.
	//
	// Against the previous block's START, not its end - see the header.
	if (mHavePrevious && start < mPreviousStartPpq - kEpsilon)
		mLastFiredBar = kNever;

	// The first bar line at or after the start of the block. floor gives
	// the line at or before it; one step forward if that is behind us.
	double line = std::floor (start / bar) * bar;
	while (line < start - kEpsilon)
		line += bar;

	int count = 0;
	while (line < end - kEpsilon && count < maxOffsets)
	{
		if (line > mLastFiredBar + kEpsilon)
		{
			// Rounded, not truncated: a line 0.9 of a sample into the
			// block belongs to the sample it is nearest.
			int offset = static_cast<int> (std::floor ((line - start) / ppqPerSample + 0.5));
			offset = std::min (numSamples - 1, std::max (0, offset));

			offsets[count++] = offset;
			mLastFiredBar = line;
		}

		line += bar;
	}

	mPreviousStartPpq = start;
	mHavePrevious = true;

	return count;
}

//------------------------------------------------------------------------
} // namespace Project6
