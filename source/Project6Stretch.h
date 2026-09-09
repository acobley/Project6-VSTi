//------------------------------------------------------------------------
// Project6 - fitting a loop to the project's tempo
//
// SDK-FREE, like Project6Dsp.* and Project6Sample.*, and for the same two
// reasons: it can be compiled and measured standalone, and the one
// function the panel and the DSP both need - fitSpeed() - has to have a
// single definition or the pad's tooltip will disagree with what is
// coming out of the speakers.
//
// WHAT THIS IS FOR
//
// A pad's file was recorded at some tempo; the project is at another. The
// fix is to play the file faster or slower by exactly the ratio between
// them, and there are only two honest ways to do it:
//
//   VARISPEED - read the file at a different rate. A 100 BPM loop in a
//     90 BPM project plays at 0.9x, so it is longer AND lower, the way a
//     tape machine slowed down is lower. Perfect quality, wrong pitch.
//     This is what a hardware sampler does and what most people mean.
//
//   PITCH-PRESERVED - keep the reading rate, and repeatedly splice the
//     playhead forwards or backwards so the file takes a different amount
//     of time to get through. Right pitch, and the splices are audible on
//     some material. This is WSOLA, described at the class below.
//
// Neither is better; they are different trades, so the pad chooses. What
// is NOT offered is a third option that pretends to be free.
//
// THE PLAYHEAD LIVES HERE, for both modes. That is the point of the
// class: the DSP asks for one output frame at a time and never has to
// know which mode produced it, and the progress bar on the pad reads the
// same number whichever mode is on, because the number it reads is the
// MUSICAL position - where we are in the loop - and not where either of
// the stretcher's two read heads happens to be sitting.
//------------------------------------------------------------------------

#pragma once

namespace Project6 {

//------------------------------------------------------------------------
/** What a pad does when its file's tempo is not the project's. */
enum class FitMode
{
	Off,            ///< play the file as it is; the tempo is ignored
	Varispeed,      ///< resample: right length, pitch moves with it
	PitchPreserved  ///< splice: right length and right pitch, with artefacts
};

constexpr int kFitModeCount = 3;

/** The default a new pad starts at.

    VARISPEED, because it is the one that cannot sound broken. A pad whose
    detection was wrong sounds obviously wrong when it is varispeeded - it
    is in the wrong key - and someone hears it and turns it off. The same
    wrong detection in pitch-preserved mode sounds like a slightly ragged
    loop, which is much easier to leave in a track by mistake. */
constexpr FitMode kDefaultFitMode = FitMode::Varispeed;

/** Long names, for the parameter's own list in the host. Never null. */
const char* fitModeName (FitMode mode);

/** Three letters, for the box in the cell, which is 26 pixels wide.
    Never null. */
const char* fitModeShortName (FitMode mode);

FitMode fitModeFromIndex (int index);
int indexOfFitMode (FitMode mode);

//------------------------------------------------------------------------
/** How far a fit is allowed to go.

    Two octaves either way is already absurd for a tempo fit - it is a
    68 BPM loop in a 272 BPM project - and anything past it is not a
    tempo difference but a DETECTION FAILURE: a one-shot read as a bar,
    or a file whose length happened to suit a candidate. Clamping rather
    than refusing keeps a pad audible instead of silently doing nothing,
    and the pad's tooltip shows the detected tempo so the cause is
    visible. */
constexpr double kMinFitSpeed = 0.25;
constexpr double kMaxFitSpeed = 4.0;

/** The speed a file must play at to sit at the project's tempo.

    projectBpm / fileBpm. A 100 BPM file in a 90 BPM project returns 0.9:
    slower, longer, and - in varispeed - lower.

    Returns exactly 1.0 whenever either tempo is unknown, which is what
    makes an undetected file play untouched rather than guessed at.

    THE SHARED FUNCTION. The DSP multiplies its read step by this and the
    panel's tooltip reports it; a second copy in the editor would be a
    second opinion about what the user is hearing. */
double fitSpeed (double fileBpm, double projectBpm);

//------------------------------------------------------------------------
/** One voice's playhead, in whichever mode the pad is set to.

    VARISPEED is the whole of the simple case: one position, advanced by
    the resampling step times the fit speed, read with linear
    interpolation whose second tap wraps to frame 0 so the loop point is
    continuous.

    PITCH-PRESERVED keeps TWO positions:

      mIdeal - where the music should be. Advances by step * speed, the
               same as varispeed, and is what position() reports.
      mRead  - where the file is actually being read. Advances by step
               alone, so the pitch is the file's own.

    They drift apart at exactly the rate the tempo differs by. Every hop -
    about 34 ms - the read head JUMPS towards the ideal head, and the jump
    is placed at the offset, within a search window, where the waveform
    after the jump best continues the waveform before it. That is the
    whole of WSOLA: the splice is chosen to be inaudible rather than to be
    on a grid. The two heads are cross-faded over the overlap so the join
    is a ramp and not a step, and because the jump is measured from the
    live drift the timing SELF-CORRECTS - a hop that had to settle for a
    poor offset is made up by the next one.

    A file too short to hold two overlaps falls back to varispeed. There
    is nothing to splice to in a file that is shorter than the splice.

    At speed exactly 1.0 the heads never drift, so no splice ever fires
    and the output is bit-identical to varispeed at 1.0, which is
    bit-identical to the file. tests/StretchTests.cpp measures that. */
class TimeStretcher
{
public:
	/** Recomputes the hop, overlap and search lengths, which are all
	    defined in seconds so they mean the same thing at every rate.
	    Off the audio thread. */
	void setSampleRate (double sampleRate);

	/** Back to the start of the file, with no splice in flight. */
	void reset ();

	/** Where the music is, in source frames. This is mIdeal, and it is
	    what the pad's progress bar divides by the frame count. */
	double position () const { return mIdeal; }

	/** Move the playhead. Used when a pad is armed and relaunched from
	    frame 0. */
	void setPosition (double position);

	/** One output frame of interleaved stereo `source`.

	    `rateStep` is the file's rate over the host's - the resampling
	    step that makes a 48 k file play at the right pitch in a 44.1 k
	    session. `speed` is the tempo fit from fitSpeed() above. The two
	    are separate because they are different facts, and multiplying
	    them together before they get here would make it impossible to
	    keep the pitch while changing the length.

	    ON THE AUDIO THREAD. Allocates nothing, and the correlation search
	    is bounded by fixed tap and step counts however long the overlap
	    is at some future sample rate. */
	void next (const float* source, int frames, double rateStep, double speed,
	           FitMode mode, double& left, double& right);

private:
	/** True when this file is long enough for the splice to have
	    somewhere to land. */
	bool canSplice (int frames) const;

	/** Choose where to jump to, as an offset in source frames from the
	    read head. Called once a hop, never per sample. */
	double findSplice (const float* source, int frames, double drift) const;

	double mIdeal = 0.0;        ///< the musical position
	double mRead  = 0.0;        ///< the incoming read head
	double mPrev  = 0.0;        ///< the outgoing read head, during a fade

	int mFade       = 0;        ///< samples left in the current cross-fade
	int mSinceHop   = 0;        ///< output samples since the last splice

	int mHop      = 1500;       ///< output samples between splices
	int mOverlap  = 530;        ///< cross-fade length, in output samples
	int mSearch   = 265;        ///< how far either side of the drift to look
	int mCorrStep = 8;          ///< stride through the correlation window
	int mSeekStep = 2;          ///< stride through the search window
};

//------------------------------------------------------------------------
} // namespace Project6
