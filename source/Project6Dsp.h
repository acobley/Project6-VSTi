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
// What it does, in order:
//
//     one voice per slot   ->  fitted to the project's tempo
//                          ->  the slot's own level
//                          ->  summed with the seven others on its ROW
//                          ->  ============ the row's DIRECT OUT taps here
//                          ->  the row's level
//                          ->  summed with the seven other rows
//                          ->  the output trim
//                          ->  out
//
// The direct out is taken BEFORE the row's fader, so the fader balances
// what goes to the main mix without touching what goes to the desk.
//
// docs/routing.png is that drawn out, and tools/render-routing.py draws
// it FROM THE HEADERS - so a diagram that disagrees with this comment is
// a diagram that has not been regenerated, not a diagram that is lying.
//
// The row buses are why renderVoices works a row at a time through one
// reused scratch buffer rather than adding every voice straight into the
// output: a row's level has to scale the SUM of its pads, and eight
// separate row buffers would be eight allocations this class must not
// make.
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
#include "Project6Stretch.h"

#include <atomic>
#include <vector>

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

/** A ROW's level, on the sum of its eight pads. The same law and the same
    range as a slot's, because it is the same kind of thing one level up -
    a submix inside a mix, which may need lifting, sitting in front of the
    one stage that may not. */
constexpr double kRowLevelMinDb     = kSlotLevelMinDb;
constexpr double kRowLevelMaxDb     = kSlotLevelMaxDb;
constexpr double kRowLevelDefaultDb = kSlotLevelDefaultDb;

/** How much row scratch to keep when nobody has said how big a block will
    be. Hosts say so in setupProcessing; the tests do not, and a DSP that
    allocated on its first render would be a DSP that allocates on the
    audio thread. */
constexpr int kDefaultMaxBlockFrames = 4096;

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

	/** A ROW's level, in decibels, on the sum of its eight pads.

	    Smoothed like a slot's - but SNAPPED whenever the row is silent,
	    which is cheaper than ramping a gain nobody can hear and means a
	    fader moved while a row is quiet is already in place when a pad on
	    it starts. */
	void setRowLevelDb (int row, double decibels);
	double rowLevelGain (int row) const;

	/** How large a block the host will ask for. Sizes the row scratch, so
	    that renderVoices never allocates. Call it from setupProcessing;
	    setSampleRate leaves a workable default behind for anything that
	    does not. */
	void setMaxBlockSize (int frames);

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

	//--------------------------------------------------------------------
	// Fitting a pad's file to the project's tempo
	//
	// The two facts a fit needs arrive from opposite directions: the
	// PROJECT's tempo comes from the host, once a block, and is the same
	// for all sixty-four pads; the FILE's tempo travels inside the
	// SampleBuffer, because it was read out of the file. Neither is
	// stored twice, and the ratio between them is computed by the one
	// shared fitSpeed() in Project6Stretch.h.
	//--------------------------------------------------------------------

	/** The host's tempo, from the process context. Zero or absent means
	    NO FIT AT ALL - a host that sends no tempo plays every file as it
	    arrived, rather than against an assumed 120. */
	void setProjectTempo (double bpm);
	double projectTempo () const { return mProjectTempo; }

	/** What this pad does when its file's tempo is not the project's.
	    Per pad, because the right answer is different for a bass loop and
	    a hi-hat pattern. */
	void setSlotFitMode (int index, FitMode mode);
	FitMode slotFitMode (int index) const;

	/** Loop, or play once and stop.

	    THE DEFAULT IS LOOP, which is what this instrument is for - a bank
	    of loops - and what every pad did before there was a choice, so a
	    project saved before this parameter existed reopens behaving
	    exactly as it did.

	    Changed WHILE PLAYING is fine and takes effect at the next end of
	    the file: switching a running loop to one-shot lets it finish the
	    pass it is on. */
	void setSlotLoop (int index, bool loop);
	bool slotLoop (int index) const;

	/** The speed this pad's file is actually being played at, all things
	    considered: 1.0 when the pad is off, when either tempo is unknown,
	    or when the file is a one-shot. For the tests and the tooltip. */
	double slotFitSpeed (int index) const;

	/** Where a slot's playhead is, in source frames. For the tests.

	    THE MUSICAL POSITION, in both fit modes - where we are in the
	    loop, not where the stretcher's read head happens to be. */
	double slotPosition (int index) const;

	/** How far through its file a slot is, 0 to 1, for the panel to draw.

	    WRITTEN BY THE AUDIO THREAD, once a block, and read by the UI
	    thread - so it is an atomic, and a relaxed one: a progress bar
	    reading a value one block old is a bar that is right to within
	    eleven milliseconds, and nothing else depends on it. Zero when the
	    slot is not sounding, so a bar never lingers on a stopped pad. */
	float slotProgress (int index) const;

	/** Render `numSamples` frames of interleaved stereo into `out`.

	    `out` must hold numSamples * kChannelCount floats. A null buffer or
	    a non-positive count does nothing - process() is handed both by
	    real hosts. */
	void render (float* out, int numSamples);

	/** The same, and ALSO hand out each row's bus on its own.

	    `rowOuts` is kSlotRows interleaved stereo buffers of numSamples
	    frames each - the row's direct output. Either the array or any
	    entry in it may be null, which is how the single-buffer call above
	    is served and how a host that has deactivated a bus is served.

	    TAPPED BEFORE THE ROW'S LEVEL. That is the whole point of it: the
	    fader sets how much of the row goes into the main mix, and the
	    direct out carries the row itself regardless - which is what makes
	    the fader usable as a balance control while the desk gets the
	    untouched signal. */
	void render (float* out, float* const* rowOuts, int numSamples);

	/** Is anything on this row making a sound? The processor's per-bus
	    silence flag depends on it: a bus flagged silent while something
	    is on it gets silenced by the host. */
	bool rowSounding (int row) const;

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
		double gain     = 0.0;     ///< the declick envelope, 0..1

		/** THE PLAYHEAD, and the tempo fit that moves it. It lives in the
		    stretcher rather than here because the pitch-preserving mode
		    needs two of them and the voice must not care which mode is
		    on - see Project6Stretch.h. */
		TimeStretcher stretch;
		FitMode       fitMode = kDefaultFitMode;

		/** False makes this pad a ONE-SHOT: it plays its file once and
		    stops itself at the end rather than coming round again.

		    The stop is the ordinary fade-out, not a cut - the file's last
		    sample is no more likely to be at zero than its first, and a
		    one-shot that clicked at the end would be worse than one that
		    looped. */
		bool loop = true;

		/** The slot's own level. Two numbers because it is smoothed: the
		    target is what the parameter says, the gain is where the
		    smoother has got to. -1 means "not started", which is what
		    makes a voice snap to its level rather than ramp up to it. */
		double levelTarget = 1.0;
		double levelGain   = -1.0;

		/** The playhead as a fraction of the file, for the panel. The ONLY
		    field besides `sample` that crosses threads, and the only
		    reason it is not just read off `position` directly: a double
		    is not atomic, and a bar drawn from a half-written playhead
		    would jump about. */
		std::atomic<float> progress { 0.f };
	};

	/** Mix every sounding voice into `out`, which is expected to be
	    silent on the way in. Splits the block into chunks the row scratch
	    can hold - which in practice it always can, the host having said
	    so in setupProcessing. */
	void renderVoices (float* out, float* const* rowOuts, int numSamples);

	/** One chunk: each row summed into the scratch, COPIED to that row's
	    direct out, then scaled by the row's level and added to `out`. */
	void renderChunk (float* out, float* const* rowOuts, int numSamples);

	/** One voice, ADDED into `dest` - which is the row's scratch, not the
	    output. */
	void renderVoice (Voice& voice, float* dest, int numSamples);

	/** The speed a voice's file should play at, given its own tempo, the
	    project's, and this pad's mode. One place, called from render and
	    from the accessor, so the tooltip cannot disagree with the sound. */
	double speedForVoice (const Voice& voice, const SampleBuffer* sample) const;

	/** One row's bus. Only a gain: the summing is the scratch buffer's
	    job and happens before this is applied. */
	struct RowBus
	{
		double target = 1.0;
		double gain   = -1.0;    ///< -1 means "not started"; snaps
	};

	Voice mVoices[kSlotCount];
	RowBus mRows[kSlotRows];

	/** ONE row's worth of interleaved stereo, reused for all eight. Sized
	    off the audio thread and never grown on it. */
	std::vector<float> mRowScratch;

	/** How far the declick envelope moves in one sample. */
	double mDeclickStep = 1.0;

	double mSampleRate = 44100.0;

	/** The host's tempo, or 0 when it has not said. ONE VALUE for all
	    sixty-four pads, because there is one project. */
	double mProjectTempo = 0.0;

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
