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

	// Linear, and never zero: at an absurd rate this must still be a step
	// that gets there rather than one that stalls a voice mid-fade.
	mDeclickStep = 1.0 / std::max (1.0, kVoiceDeclickSeconds * mSampleRate);

	reset ();
}

//------------------------------------------------------------------------
void Project6Dsp::reset ()
{
	mGain = -1.0;                        // snap to the target next block

	// Every voice stops DEAD, not fading: reset is called when the host
	// deactivates or re-prepares the plug-in, and there is no block
	// coming in which a fade could be rendered. The samples themselves
	// are left published - they are still loaded, they are just not
	// playing.
	for (Voice& voice : mVoices)
	{
		voice.sounding = false;
		voice.stopping = false;
		voice.position = 0.0;
		voice.gain     = 0.0;
		voice.levelGain = -1.0;      // snap on the next start
	}
}

//------------------------------------------------------------------------
void Project6Dsp::setSlotSample (int index, const SampleBuffer* sample)
{
	if (!isSlotIndex (index))
		return;

	// RELEASE, so that an audio thread which acquires this pointer also
	// sees every byte the UI thread wrote into the buffer before storing
	// it. Without the ordering the pointer can arrive before its contents
	// on a weakly ordered machine - which is every Apple Silicon Mac.
	mVoices[index].sample.store (sample, std::memory_order_release);
}

//------------------------------------------------------------------------
const SampleBuffer* Project6Dsp::slotSample (int index) const
{
	if (!isSlotIndex (index))
		return nullptr;

	return mVoices[index].sample.load (std::memory_order_acquire);
}

//------------------------------------------------------------------------
void Project6Dsp::setSlotLevelDb (int index, double decibels)
{
	if (!isSlotIndex (index))
		return;

	// THE SHARED CONVERSION, so a bar at the bottom of its travel is off
	// here for the same reason the panel draws it as off.
	mVoices[index].levelTarget = dbToLinear (
		std::min (kSlotLevelMaxDb, std::max (kSlotLevelMinDb, decibels)),
		kSlotLevelMinDb);
}

//------------------------------------------------------------------------
double Project6Dsp::slotLevelGain (int index) const
{
	if (!isSlotIndex (index))
		return 0.0;

	const Voice& voice = mVoices[index];
	return (voice.levelGain < 0.0) ? voice.levelTarget : voice.levelGain;
}

//------------------------------------------------------------------------
void Project6Dsp::setSlotPlaying (int index, bool playing)
{
	if (!isSlotIndex (index))
		return;

	Voice& voice = mVoices[index];

	if (playing)
	{
		if (!voice.sounding)
		{
			// FROM THE BEGINNING. A pad you click plays its sample, not
			// the middle of it.
			voice.sounding = true;
			voice.stopping = false;
			voice.position = 0.0;
			voice.gain     = 0.0;

			// SNAP the level. A pad set to -20 dB comes in at -20 dB; a
			// smoother left to ramp there from the last value would make
			// the first ten milliseconds of every launch the wrong
			// loudness.
			voice.levelGain = -1.0;
		}
		else
		{
			// Caught mid-fade. Reverse the envelope where it is rather
			// than jumping the playhead back to zero: the jump would be
			// exactly the click the fade is there to prevent.
			voice.stopping = false;
		}
	}
	else if (voice.sounding)
	{
		voice.stopping = true;
	}
}

//------------------------------------------------------------------------
bool Project6Dsp::slotSounding (int index) const
{
	if (!isSlotIndex (index))
		return false;

	return mVoices[index].sounding;
}

//------------------------------------------------------------------------
int Project6Dsp::soundingVoiceCount () const
{
	int count = 0;
	for (const Voice& voice : mVoices)
		if (voice.sounding)
			++count;
	return count;
}

//------------------------------------------------------------------------
double Project6Dsp::slotPosition (int index) const
{
	if (!isSlotIndex (index))
		return 0.0;

	return mVoices[index].position;
}

//------------------------------------------------------------------------
void Project6Dsp::renderVoices (float* out, int numSamples)
{
	for (Voice& voice : mVoices)
	{
		if (!voice.sounding)
			continue;

		const SampleBuffer* sample = voice.sample.load (std::memory_order_acquire);

		// The slot was emptied, or its file failed to load, while the
		// voice was running. Stop rather than reading a null pointer -
		// and stop DEAD, because there is nothing left to fade out of.
		if (sample == nullptr || sample->frameCount <= 0
		    || sample->samples.size ()
		           < static_cast<size_t> (sample->frameCount) * kSampleChannels)
		{
			voice.sounding = false;
			voice.stopping = false;
			voice.gain     = 0.0;
			continue;
		}

		const int frames = sample->frameCount;
		const float* source = sample->samples.data ();

		// PLAY AT THE FILE'S OWN PITCH. A 48 k file in a 44.1 k session
		// has to advance 1.088 source frames per output frame or it plays
		// flat, and the sample rate is the host's to choose.
		//
		// Linear interpolation, which is what a sampler of this shape can
		// justify: it is a gentle low-pass on the way up and aliases on
		// the way down, both mildly at the ratios real files produce.
		// Anything better is a resampler, and a resampler is a decision
		// about latency and cost that belongs with the rest of the DSP.
		const double step = (mSampleRate > 0.0) ? sample->sourceRate / mSampleRate : 1.0;

		for (int i = 0; i < numSamples; ++i)
		{
			// The envelope first, so a voice that reaches zero this
			// sample contributes nothing further.
			// The slot's own level, smoothed with the same one-pole the
			// output trim uses. Snapped on the first sample of a voice -
			// see setSlotPlaying - and ramped from then on, so a bar
			// dragged while a pad is running does not zipper.
			if (voice.levelGain < 0.0)
				voice.levelGain = voice.levelTarget;
			else
				voice.levelGain += (voice.levelTarget - voice.levelGain) * mCoeff;

			if (voice.stopping)
			{
				voice.gain -= mDeclickStep;
				if (voice.gain <= 0.0)
				{
					voice.gain     = 0.0;
					voice.sounding = false;
					voice.stopping = false;
					break;
				}
			}
			else if (voice.gain < 1.0)
			{
				voice.gain = std::min (1.0, voice.gain + mDeclickStep);
			}

			int first = static_cast<int> (voice.position);
			if (first < 0 || first >= frames)
				first = 0;                  // belt and braces against a rounding edge
			const double fraction = voice.position - static_cast<double> (first);

			// THE SECOND TAP WRAPS TO FRAME 0, so the interpolation is
			// continuous across the loop point instead of fading into the
			// last frame and jumping.
			const int second = (first + 1 < frames) ? first + 1 : 0;

			const size_t a = static_cast<size_t> (first) * kSampleChannels;
			const size_t b = static_cast<size_t> (second) * kSampleChannels;

			const double left  = source[a]     + (source[b]     - source[a])     * fraction;
			const double right = source[a + 1] + (source[b + 1] - source[a + 1]) * fraction;

			// The declick envelope and the slot's level, in that order
			// and both before the mix. At the default level of 0 dB the
			// multiply is by exactly 1.0, so a pad at full envelope is
			// still bit-identical to its file - which DspTests checks.
			const double voiceGain = voice.gain * voice.levelGain;

			out[static_cast<size_t> (i) * kChannelCount]
				+= static_cast<float> (left * voiceGain);
			out[static_cast<size_t> (i) * kChannelCount + 1]
				+= static_cast<float> (right * voiceGain);

			voice.position += step;
			if (voice.position >= frames)
			{
				// fmod rather than a subtraction: a one-frame sample at a
				// large rate ratio can pass the end several times over in
				// a single output sample.
				voice.position = std::fmod (voice.position, static_cast<double> (frames));
			}
		}
	}
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

	// Silence first: renderVoices ADDS, so the mix has to start empty.
	std::fill_n (out, static_cast<size_t> (numSamples) * kChannelCount, 0.f);

	renderVoices (out, numSamples);

	// The trim is the last stage, over the whole mix, and stays there.
	applyOutputTrim (out, numSamples);
}

//------------------------------------------------------------------------
} // namespace Project6
