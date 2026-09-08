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
const char* divisionShortName (LaunchDivision division)
{
	switch (division)
	{
		case LaunchDivision::Bar:     return "1/1";
		case LaunchDivision::HalfBar: return "1/2";
		case LaunchDivision::Quarter: return "1/4";
		case LaunchDivision::Eighth:  return "1/8";
	}
	return "1/1";
}

//------------------------------------------------------------------------
const char* divisionName (LaunchDivision division)
{
	switch (division)
	{
		case LaunchDivision::Bar:     return "Bar";
		case LaunchDivision::HalfBar: return "1/2 bar";
		case LaunchDivision::Quarter: return "1/4 bar";
		case LaunchDivision::Eighth:  return "1/8 bar";
	}
	return "Bar";
}

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
	mLastFiredLine    = kNever;
	mPreviousStartPpq = kNever;
	mHavePrevious     = false;
}

//------------------------------------------------------------------------
int BarClock::gridLinesInBlock (const TransportInfo& info, int numSamples, double sampleRate,
                                GridLine* lines, int maxLines)
{
	if (lines == nullptr || maxLines <= 0)
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
	// guard. Without this a one-bar cycle plays its lines once and then
	// never again, because the same lines keep arriving and keep being
	// recognised as ones already fired.
	//
	// Against the previous block's START, not its end - see the header.
	if (mHavePrevious && start < mPreviousStartPpq - kEpsilon)
		mLastFiredLine = kNever;

	// THE FINEST DIVISION is what the grid is made of; the coarser ones
	// are steps of it, which is why one list serves every slot.
	const double stepLength = bar / static_cast<double> (kGridStepsPerBar);

	// The first line at or after the start of the block, and which step of
	// the bar it is. floor gives the line at or before it; one step
	// forward if that is behind us.
	double whichStep = std::floor (start / stepLength);
	double line = whichStep * stepLength;
	while (line < start - kEpsilon)
	{
		line += stepLength;
		whichStep += 1.0;
	}

	int count = 0;
	while (line < end - kEpsilon && count < maxLines)
	{
		if (line > mLastFiredLine + kEpsilon)
		{
			// Rounded, not truncated: a line 0.9 of a sample into the
			// block belongs to the sample it is nearest.
			int offset = static_cast<int> (std::floor ((line - start) / ppqPerSample + 0.5));
			offset = std::min (numSamples - 1, std::max (0, offset));

			// Step WITHIN THE BAR, so step 0 is the bar line. The modulo
			// is taken on a double because a project position runs to
			// thousands of steps and the count must not be built up by
			// addition, which would drift.
			const double intoBar = whichStep
			    - std::floor (whichStep / static_cast<double> (kGridStepsPerBar))
			          * static_cast<double> (kGridStepsPerBar);

			lines[count].offset = offset;
			lines[count].step = static_cast<int> (intoBar + 0.5) % kGridStepsPerBar;
			++count;

			mLastFiredLine = line;
		}

		line += stepLength;
		whichStep += 1.0;
	}

	mPreviousStartPpq = start;
	mHavePrevious = true;

	return count;
}

//------------------------------------------------------------------------
} // namespace Project6
