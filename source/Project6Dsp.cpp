//------------------------------------------------------------------------
// Project6 - the audio line, implementation
//
// SDK-free. If a Steinberg header ever appears in this file the standalone
// test build stops working - see the banner in Project6Dsp.h.
//------------------------------------------------------------------------

#include "Project6Dsp.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Project6 {

//------------------------------------------------------------------------
double dbToLinear (double decibels, double minDb)
{
	// At or below the bottom of travel the control is OFF. Returning
	// 10^(-60/20) instead would leave a millivolt of whatever is playing
	// audible on a slider the user has pulled all the way down.
	if (decibels <= minDb)
		return 0.0;
	return std::pow (10.0, decibels / 20.0);
}

//------------------------------------------------------------------------
double linearToDb (double gain, double minDb)
{
	if (gain <= 0.0)
		return minDb;
	return std::max (minDb, 20.0 * std::log10 (gain));
}

//------------------------------------------------------------------------
void Project6Dsp::setSampleRate (double sampleRate)
{
	if (sampleRate > 0.0)
		mSampleRate = sampleRate;

	// One-pole towards the target. Recomputed here and nowhere else, so a
	// rate change cannot leave the smoother running at the old rate's
	// speed - which is how a ramp becomes twice as long at 96 k.
	mCoeff = 1.0 - std::exp (-1.0 / (kTrimSmoothingSeconds * mSampleRate));

	reset ();
}

//------------------------------------------------------------------------
void Project6Dsp::reset ()
{
	mGain = -1.0;                        // snap to the target next block
}

//------------------------------------------------------------------------
void Project6Dsp::setOutputTrimDb (double decibels)
{
	mTrimDb  = std::min (kTrimMaxDb, std::max (kTrimMinDb, decibels));
	mTarget  = dbToLinear (mTrimDb, kTrimMinDb);
}

//------------------------------------------------------------------------
void Project6Dsp::applyOutputTrim (float* interleaved, int numSamples)
{
	if (interleaved == nullptr || numSamples <= 0)
		return;

	if (mGain < 0.0)
		mGain = mTarget;                 // first block, no ramp

	for (int i = 0; i < numSamples; ++i)
	{
		mGain += (mTarget - mGain) * mCoeff;
		const float g = static_cast<float> (mGain);
		for (int ch = 0; ch < kChannelCount; ++ch)
			interleaved[static_cast<size_t> (i) * kChannelCount + ch] *= g;
	}
}

//------------------------------------------------------------------------
void Project6Dsp::render (float* out, int numSamples)
{
	if (out == nullptr || numSamples <= 0)
		return;

	// No voices yet. THE SILENCE IS THE PLACEHOLDER: the synthesis goes
	// here, writing into the same interleaved buffer, and the output stage
	// below stays where it is at the end of the chain.
	std::fill_n (out, static_cast<size_t> (numSamples) * kChannelCount, 0.f);

	applyOutputTrim (out, numSamples);
}

//------------------------------------------------------------------------
} // namespace Project6
