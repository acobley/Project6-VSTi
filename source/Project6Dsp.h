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
// Nothing is synthesised yet. render() writes silence and then runs the
// output stage over it, so the host -> parameter -> DSP chain is complete
// and exercised from the first build; the voices go in underneath it.
//------------------------------------------------------------------------

#pragma once

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

/** Interleaved stereo throughout, as every one of these ports has been. */
constexpr int kChannelCount = 2;

/** How long the output trim takes to reach a new setting. About 10 ms is
    inaudible on a fader move and far faster than anyone can turn a knob. */
constexpr double kTrimSmoothingSeconds = 0.01;

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
