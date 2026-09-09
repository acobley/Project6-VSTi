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

	// A workable default so that anything which never calls
	// setMaxBlockSize - the tests, mostly - still renders without the
	// audio thread having to allocate. A host overrides it in
	// setupProcessing with the size it actually intends to ask for.
	if (mRowScratch.empty ())
		setMaxBlockSize (kDefaultMaxBlockFrames);

	// The stretcher's hop, overlap and search are all defined in seconds,
	// so every voice has to be told the rate or a splice would be a
	// different length of time at 96 k than at 44.1 k.
	for (Voice& voice : mVoices)
		voice.stretch.setSampleRate (mSampleRate);

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
		voice.stretch.reset ();
		voice.gain     = 0.0;
		voice.levelGain = -1.0;      // snap on the next start
		voice.progress.store (0.f, std::memory_order_relaxed);
	}

	for (RowBus& bus : mRows)
		bus.gain = -1.0;
}

//------------------------------------------------------------------------
void Project6Dsp::setMaxBlockSize (int frames)
{
	// OFF THE AUDIO THREAD. renderVoices chunks to whatever this leaves
	// behind rather than growing it, so a host that lies about its
	// maximum costs a second pass and not an allocation.
	mRowScratch.assign (static_cast<size_t> (std::max (1, frames)) * kChannelCount, 0.f);
}

//------------------------------------------------------------------------
void Project6Dsp::setRowLevelDb (int row, double decibels)
{
	if (!isRowIndex (row))
		return;

	mRows[row].target = dbToLinear (
		std::min (kRowLevelMaxDb, std::max (kRowLevelMinDb, decibels)), kRowLevelMinDb);
}

//------------------------------------------------------------------------
double Project6Dsp::rowLevelGain (int row) const
{
	if (!isRowIndex (row))
		return 0.0;

	const RowBus& bus = mRows[row];
	return (bus.gain < 0.0) ? bus.target : bus.gain;
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
			voice.stretch.setPosition (0.0);
			voice.gain     = 0.0;
			voice.progress.store (0.f, std::memory_order_relaxed);

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
float Project6Dsp::slotProgress (int index) const
{
	if (!isSlotIndex (index))
		return 0.f;

	return mVoices[index].progress.load (std::memory_order_relaxed);
}

//------------------------------------------------------------------------
double Project6Dsp::slotPosition (int index) const
{
	if (!isSlotIndex (index))
		return 0.0;

	return mVoices[index].stretch.position ();
}

//------------------------------------------------------------------------
void Project6Dsp::setProjectTempo (double bpm)
{
	// Not clamped and not defaulted. Zero means "the host did not say",
	// and fitSpeed turns that into a speed of exactly 1.
	mProjectTempo = (bpm > 0.0) ? bpm : 0.0;
}

//------------------------------------------------------------------------
void Project6Dsp::setSlotFitMode (int index, FitMode mode)
{
	if (!isSlotIndex (index))
		return;

	// CHANGED WHILE PLAYING IS FINE. The stretcher keeps the musical
	// position in mIdeal whichever mode is running, so switching modes
	// mid-loop changes how the next sample is produced without moving
	// where in the bar we are.
	mVoices[index].fitMode = mode;
}

//------------------------------------------------------------------------
FitMode Project6Dsp::slotFitMode (int index) const
{
	if (!isSlotIndex (index))
		return FitMode::Off;

	return mVoices[index].fitMode;
}

//------------------------------------------------------------------------
double Project6Dsp::speedForVoice (const Voice& voice, const SampleBuffer* sample) const
{
	if (voice.fitMode == FitMode::Off || sample == nullptr)
		return 1.0;

	// fittable() is where the one-shot rule lives: a hit is never
	// stretched, whatever the pad is set to, because its length says
	// nothing about a tempo.
	if (!sample->fittable ())
		return 1.0;

	return fitSpeed (sample->tempoBpm, mProjectTempo);
}

//------------------------------------------------------------------------
double Project6Dsp::slotFitSpeed (int index) const
{
	if (!isSlotIndex (index))
		return 1.0;

	const Voice& voice = mVoices[index];
	return speedForVoice (voice, voice.sample.load (std::memory_order_acquire));
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
bool Project6Dsp::rowSounding (int row) const
{
	if (!isRowIndex (row))
		return false;

	for (int column = 0; column < kSlotColumns; ++column)
		if (mVoices[slotIndex (column, row)].sounding)
			return true;

	return false;
}

//------------------------------------------------------------------------
void Project6Dsp::renderVoices (float* out, float* const* rowOuts, int numSamples)
{
	const int capacity = static_cast<int> (mRowScratch.size () / kChannelCount);
	if (capacity <= 0)
		return;                     // setSampleRate has not been called

	// Chunked only because the scratch might be smaller than the block -
	// which it is not, in a host that told us its maximum. Growing it
	// here instead would be an allocation on the audio thread.
	int done = 0;
	while (done < numSamples)
	{
		const int chunk = std::min (numSamples - done, capacity);

		// The row buffers have to be advanced with the output, and a
		// null one has to stay null. A fixed-size array on the stack:
		// eight pointers, no allocation.
		float* chunkRows[kSlotRows] = { nullptr };
		if (rowOuts != nullptr)
		{
			for (int row = 0; row < kSlotRows; ++row)
				if (rowOuts[row] != nullptr)
					chunkRows[row] = rowOuts[row]
					                 + static_cast<size_t> (done) * kChannelCount;
		}

		renderChunk (out + static_cast<size_t> (done) * kChannelCount,
		             (rowOuts != nullptr) ? chunkRows : nullptr, chunk);
		done += chunk;
	}
}

//------------------------------------------------------------------------
void Project6Dsp::renderChunk (float* out, float* const* rowOuts, int numSamples)
{
	const size_t samples = static_cast<size_t> (numSamples) * kChannelCount;

	for (int row = 0; row < kSlotRows; ++row)
	{
		RowBus& bus = mRows[row];

		// IS ANYTHING ON THIS ROW SOUNDING? Seven rows out of eight
		// usually are not, and the cost of the answer is eight bools.
		bool anySounding = false;
		for (int column = 0; column < kSlotColumns; ++column)
			anySounding |= mVoices[slotIndex (column, row)].sounding;

		if (!anySounding)
		{
			// SNAPPED, not ramped. Nothing can hear this row, so there is
			// nothing to smooth - and a fader moved while a row is silent
			// is then already in place when a pad on it starts, instead
			// of sliding into position over the first ten milliseconds.
			//
			// The row's direct out is left alone: render() cleared it,
			// and a silent row sends silence.
			bus.gain = bus.target;
			continue;
		}

		std::fill_n (mRowScratch.begin (), samples, 0.f);

		// The row's eight pads, each through its own level, summed.
		for (int column = 0; column < kSlotColumns; ++column)
		{
			Voice& voice = mVoices[slotIndex (column, row)];
			if (voice.sounding)
				renderVoice (voice, mRowScratch.data (), numSamples);
		}

		// THE DIRECT OUT TAPS HERE - after the pads and their own levels
		// are summed, and before this row's fader is anywhere near it.
		// A copy rather than an add, because one row writes one bus.
		if (rowOuts != nullptr && rowOuts[row] != nullptr)
			std::copy_n (mRowScratch.begin (), samples, rowOuts[row]);

		// The row's level on that sum, and the result added to the mix.
		// Smoothed per sample, like every other gain here.
		if (bus.gain < 0.0)
			bus.gain = bus.target;

		for (int i = 0; i < numSamples; ++i)
		{
			bus.gain += (bus.target - bus.gain) * mCoeff;

			const size_t at = static_cast<size_t> (i) * kChannelCount;
			out[at]     += static_cast<float> (mRowScratch[at]     * bus.gain);
			out[at + 1] += static_cast<float> (mRowScratch[at + 1] * bus.gain);
		}
	}
}

//------------------------------------------------------------------------
void Project6Dsp::renderVoice (Voice& voice, float* dest, int numSamples)
{
	const SampleBuffer* sample = voice.sample.load (std::memory_order_acquire);

	// The slot was emptied, or its file failed to load, while the voice
	// was running. Stop rather than reading a null pointer - and stop
	// DEAD, because there is nothing left to fade out of.
	if (sample == nullptr || sample->frameCount <= 0
	    || sample->samples.size ()
	           < static_cast<size_t> (sample->frameCount) * kSampleChannels)
	{
		voice.sounding = false;
		voice.stopping = false;
		voice.gain     = 0.0;
		voice.progress.store (0.f, std::memory_order_relaxed);
		return;
	}

	const int frames = sample->frameCount;
	const float* source = sample->samples.data ();

	// PLAY AT THE FILE'S OWN PITCH. A 48 k file in a 44.1 k session has to
	// advance 1.088 source frames per output frame or it plays flat, and
	// the sample rate is the host's to choose.
	const double step = (mSampleRate > 0.0) ? sample->sourceRate / mSampleRate : 1.0;

	// AND THEN AT THE PROJECT'S TEMPO, which is a SEPARATE multiplier and
	// is kept separate all the way into the stretcher: the resampling
	// step is a fact about the file's sample rate and the fit is a fact
	// about its tempo, and the pitch-preserving mode works precisely by
	// applying one of them to its read head and the other to its clock.
	const double speed = speedForVoice (voice, sample);
	const FitMode mode = voice.fitMode;

	for (int i = 0; i < numSamples; ++i)
	{
		// The slot's own level, smoothed with the same one-pole the
		// output trim uses. Snapped on the first sample of a voice - see
		// setSlotPlaying - and ramped from then on, so a bar dragged
		// while a pad is running does not zipper.
		if (voice.levelGain < 0.0)
			voice.levelGain = voice.levelTarget;
		else
			voice.levelGain += (voice.levelTarget - voice.levelGain) * mCoeff;

		// The envelope next, so a voice that reaches zero this sample
		// contributes nothing further.
		if (voice.stopping)
		{
			voice.gain -= mDeclickStep;
			if (voice.gain <= 0.0)
			{
				voice.gain     = 0.0;
				voice.sounding = false;
				voice.stopping = false;
				voice.progress.store (0.f, std::memory_order_relaxed);
				break;
			}
		}
		else if (voice.gain < 1.0)
		{
			voice.gain = std::min (1.0, voice.gain + mDeclickStep);
		}

		// ONE FRAME FROM THE PLAYHEAD, whichever mode it is running in.
		// Linear interpolation with the second tap wrapping to frame 0
		// still, so the loop point is continuous - that read now lives in
		// the stretcher, where both modes can share it.
		double left = 0.0, right = 0.0;
		voice.stretch.next (source, frames, step, speed, mode, left, right);

		// The declick envelope and the slot's level, in that order and
		// both before the row bus. At the default level of 0 dB the
		// multiply is by exactly 1.0, so a pad at full envelope is still
		// bit-identical to its file - which DspTests checks.
		const double voiceGain = voice.gain * voice.levelGain;

		dest[static_cast<size_t> (i) * kChannelCount]
			+= static_cast<float> (left * voiceGain);
		dest[static_cast<size_t> (i) * kChannelCount + 1]
			+= static_cast<float> (right * voiceGain);

	}

	// WHERE THE PLAYHEAD ENDED UP, for the panel to draw - once a block
	// and not once a sample, because nothing reads it faster than the
	// editor's timer and a store per sample would be sixty-four atomic
	// writes per frame to move a bar a pixel every few hundred.
	//
	// Guarded on still sounding: a voice that stopped during this call
	// has already zeroed it, and must not have it put back.
	if (voice.sounding)
	{
		voice.progress.store (
			static_cast<float> (voice.stretch.position () / static_cast<double> (frames)),
			std::memory_order_relaxed);
	}
}

//------------------------------------------------------------------------
void Project6Dsp::render (float* out, int numSamples)
{
	render (out, nullptr, numSamples);
}

//------------------------------------------------------------------------
void Project6Dsp::render (float* out, float* const* rowOuts, int numSamples)
{
	if (out == nullptr || numSamples <= 0)
		return;

	// Silence first: renderVoices ADDS into the mix, so it has to start
	// empty - and the row buses have to start empty too, because a row
	// with nothing sounding is skipped entirely and would otherwise send
	// whatever was in the host's buffer last block.
	const size_t samples = static_cast<size_t> (numSamples) * kChannelCount;
	std::fill_n (out, samples, 0.f);

	if (rowOuts != nullptr)
	{
		for (int row = 0; row < kSlotRows; ++row)
			if (rowOuts[row] != nullptr)
				std::fill_n (rowOuts[row], samples, 0.f);
	}

	// Pads -> slot levels -> row buses -> (direct outs) -> row levels ->
	// here.
	renderVoices (out, rowOuts, numSamples);

	// The trim is the last stage, over the whole mix, and stays there.
	// IT DOES NOT TOUCH THE DIRECT OUTS: those left before the row fader
	// and are certainly not going through the master.
	applyOutputTrim (out, numSamples);
}

//------------------------------------------------------------------------
} // namespace Project6
