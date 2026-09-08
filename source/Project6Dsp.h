//------------------------------------------------------------------------
// Project6 - the audio line
//
// NO SDK HEADER MAY ENTER THIS FILE OR ITS .cpp. That is deliberate, and
// it buys two things:
//
//   * the class compiles and runs standalone with plain
//         c++ -std=c++17 tests/DspTests.cpp source/Project6Dsp.cpp
//     so its numbers can be checked before anything is built, and long
//     before a host is involved;
//
//   * VST3 splits the processor from the controller into two objects that
//     do not share memory. ANYTHING THE EDITOR DISPLAYS THAT THE DSP ALSO
//     COMPUTES MUST COME FROM ONE SHARED FUNCTION BOTH CALL, or the panel
//     agrees with the filter today and diverges at some sample rate
//     nobody tests. Those functions live here - dbToLinear() below is the
//     first of them, and the response display already calls it rather
//     than converting decibels itself.
//
// What it does: sixty-four looping sample voices, one per slot, mixed and
// passed through the output trim.
//
// THE SAMPLES ARE NOT OWNED HERE. Project6Processor loads a file on the UI
// thread, keeps the buffer alive, and publishes a bare pointer through
// setSlotSample(); the audio thread only ever READS that pointer. That is
// the whole thread story, and it works only because a SampleBuffer is
// immutable once published and because the processor does not free the old
// one until the audio thread has demonstrably moved past it - see the
// retire list in Project6Processor.
//------------------------------------------------------------------------

#pragma once

#include "Project6Sample.h"
#include "Project6Slots.h"

#include <atomic>

namespace Project6 {

//------------------------------------------------------------------------
// The output trim
//
// The top of travel is UNITY, not a boost. A blank instrument has nothing
// to make up for, and a trim whose default is above unity hides clipping
// in whatever is added later - the measurement in tests/DspTests.cpp
// section 5 exists to keep that honest.
//------------------------------------------------------------------------
constexpr double kTrimMinDb     = -60.0;
constexpr double kTrimMaxDb     =   0.0;
constexpr double kTrimDefaultDb =   0.0;

//------------------------------------------------------------------------
// A slot's own level
//
// -40 to +12 dB, the range VocalFilter gives a formant's level, and for
// the same reason: this is a COMPONENT level inside a mix, not the master.
// A component may need lifting - a quiet sample in a slot with no way to
// bring it up is a slot you cannot use - and the stage that must not
// boost is the output trim above, which tops out at unity and is where
// clipping is answered.
//
// The bottom of the travel is SILENCE, not -40 dB of leakage: a bar
// pulled all the way down is off, exactly as the trim's is.
//------------------------------------------------------------------------
constexpr double kSlotLevelMinDb     = -40.0;
constexpr double kSlotLevelMaxDb     =  12.0;
constexpr double kSlotLevelDefaultDb =   0.0;

/** Interleaved stereo throughout, as every one of these ports has been. */
constexpr int kChannelCount = 2;

/** How long the output trim takes to reach a new setting. About 10 ms is
    inaudible on a fader move and far faster than anyone can turn a knob. */
constexpr double kTrimSmoothingSeconds = 0.01;

/** How long a voice takes to fade in when it starts and out when it stops.

    A LOOP DOES NOT START AT ZERO. Cutting a sample in at full gain on
    whatever value happens to be at frame 0, and cutting it out on whatever
    is under the playhead when you click again, is a click both times - and
    a click on every one of sixty-four pads is what makes a sampler sound
    cheap. Five milliseconds is short enough that a drum transient survives
    it and long enough to remove the step.

    The ramp is LINEAR, not the one-pole the trim uses: an exponential
    approaches zero without reaching it, so a voice asked to stop would
    never actually finish and would sit in the mix for ever. */
constexpr double kVoiceDeclickSeconds = 0.005;

/** Decibels to a linear gain, with a floor that means SILENCE rather than
    a very small number. `minDb` is the bottom of the control's travel: a
    slider at its minimum must be off, not -60 dB of leakage.

    THE SHARED FUNCTION. The DSP scales by this and the panel draws by it;
    a private copy in either would be a second opinion. */
double dbToLinear (double decibels, double minDb);

/** Linear gain back to decibels, floored at `minDb`. The inverse of the
    above, so a measurement can be reported in the units the panel uses. */
double linearToDb (double gain, double minDb);

//------------------------------------------------------------------------
class Project6Dsp
{
public:
	Project6Dsp () = default;

	/** Recomputes every rate-dependent coefficient. Called from
	    setupProcessing, never from the audio thread's inner loop. */
	void setSampleRate (double sampleRate);
	double sampleRate () const { return mSampleRate; }

	/** Back to the start: silence, and the trim snapping rather than
	    ramping on the next block. */
	void reset ();

	/** The trim's target, in decibels. Smoothed towards, not jumped to. */
	void setOutputTrimDb (double decibels);
	double outputTrimDb () const { return mTrimDb; }

	/** Where the smoother actually is, as a linear gain. -1 before the
	    first block, which is what makes that block snap. */
	double currentGain () const { return (mGain < 0.0) ? mTarget : mGain; }

	//--------------------------------------------------------------------
	// The sample slots
	//--------------------------------------------------------------------

	/** Publish a slot's decoded audio, or nullptr to empty it.

	    CALLED ON THE UI THREAD, never the audio thread. The store is
	    atomic and release-ordered, so an audio thread that sees the new
	    pointer also sees the buffer's contents. THE CALLER KEEPS THE
	    BUFFER ALIVE - this class stores the pointer and nothing else. */
	void setSlotSample (int index, const SampleBuffer* sample);

	/** What a slot currently points at. Mostly for the tests. */
	const SampleBuffer* slotSample (int index) const;

	/** A slot's own level, in decibels.

	    Called from the audio thread once per block, like the trigger.
	    SMOOTHED, not stepped: a level moved by a mouse drag or an
	    automation lane changes every block, and a block-rate step on a
	    gain is a click on every buffer boundary. It SNAPS when a voice
	    starts, though - a pad set to -20 dB should come in at -20 dB, not
	    ramp there from wherever the smoother happened to be. */
	void setSlotLevelDb (int index, double decibels);

	/** Where a slot's level smoother actually is, as a linear gain. For
	    the tests. */
	double slotLevelGain (int index) const;

	/** Start or stop a slot's loop.

	    Called from the audio thread, once per block, with the slot's
	    trigger parameter. A rising edge starts FROM THE BEGINNING; a
	    falling edge fades out and then stops. Re-triggering during the
	    fade-out is a change of mind: the fade reverses where it is rather
	    than jumping back to frame 0, which would be the click the fade
	    exists to avoid. */
	void setSlotPlaying (int index, bool playing);

	/** True while a slot is producing audio - including during its
	    fade-out, which still has to be mixed. */
	bool slotSounding (int index) const;

	/** How many are. The processor's silence flag depends on this: a synth
	    that flags silence while something is playing gets silenced by the
	    host. */
	int soundingVoiceCount () const;

	/** Where a slot's playhead is, in source frames. For the tests. */
	double slotPosition (int index) const;

	/** Render `numSamples` frames of interleaved stereo into `out`.

	    There are no voices yet, so this clears the buffer and then runs
	    the output stage over it. `out` must hold numSamples *
	    kChannelCount floats. A null buffer or a non-positive count does
	    nothing - process() is handed both by real hosts. */
	void render (float* out, int numSamples);

	/** The output stage on its own, applied in place.

	    Separate from render() so it can be TESTED. A synth that renders
	    silence cannot prove its own gain stage: multiply silence by
	    anything and it is still silence, so a broken trim would pass
	    every check. Handing this a known signal is what makes the unity
	    and smoothing assertions in tests/DspTests.cpp mean something. */
	void applyOutputTrim (float* interleaved, int numSamples);

private:
	//--------------------------------------------------------------------
	/** One slot's playback state.

	    Everything except `sample` is touched by the AUDIO THREAD ONLY, so
	    none of it needs to be atomic. `sample` is the one field the UI
	    thread writes, and it is the only atomic in the class. */
	struct Voice
	{
		std::atomic<const SampleBuffer*> sample { nullptr };

		bool   sounding = false;   ///< mixing, fade-out included
		bool   stopping = false;   ///< fading out, will stop when gain hits 0
		double position = 0.0;     ///< in SOURCE frames, fractional
		double gain     = 0.0;     ///< the declick envelope, 0..1

		/** The slot's own level. Two numbers because it is smoothed: the
		    target is what the parameter says, the gain is where the
		    smoother has got to. -1 means "not started", which is what
		    makes a voice snap to its level rather than ramp up to it. */
		double levelTarget = 1.0;
		double levelGain   = -1.0;
	};

	/** Mix every sounding voice into `out`, which is expected to be
	    silent on the way in. */
	void renderVoices (float* out, int numSamples);

	Voice mVoices[kSlotCount];

	/** How far the declick envelope moves in one sample. */
	double mDeclickStep = 1.0;

	double mSampleRate = 44100.0;
	double mTrimDb     = kTrimDefaultDb;
	double mTarget     = 1.0;
	/** -1 means "not started": the first block snaps to the target rather
	    than ramping up from zero, which would be an audible fade-in on
	    every single start. */
	double mGain  = -1.0;
	double mCoeff = 1.0;
};

//------------------------------------------------------------------------
} // namespace Project6
