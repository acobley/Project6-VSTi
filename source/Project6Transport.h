//------------------------------------------------------------------------
// Project6 - the bar clock
//
// SDK-FREE, and this one is the file that most needed to be. Working out
// where a bar line falls inside a block is arithmetic with four ways to be
// subtly wrong - an off-by-one on a line that lands exactly at offset 0, a
// line fired twice across two blocks, a line never fired again after the
// host loops back over it, a bar length that assumes 4/4 - and none of
// those show up as a crash. They show up as a pad that starts a bar late,
// once, on somebody else's machine.
//
// So the arithmetic takes plain doubles, knows nothing about VST3, and is
// tested in tests/TransportTests.cpp. Project6Processor's job is only to
// copy the host's ProcessContext into a TransportInfo and act on the
// offsets that come back.
//------------------------------------------------------------------------

#pragma once

namespace Project6 {

//------------------------------------------------------------------------
// The launch grid
//
// A slot does not have to wait for a whole bar. Each one picks how finely
// it is quantised, and the choices are FRACTIONS OF A BAR:
//
//     1/1  the bar line          1/4  a quarter of the bar
//     1/2  half way through      1/8  an eighth of it
//
// Fractions of a bar and not note values, which matters only outside 4/4 -
// there the two coincide, a quarter of a 4/4 bar being a quarter note. In
// 7/8 they part company, and the fraction is the right answer: every
// division line then still NESTS inside the bar, so a slot on 1/8 and a
// slot on 1/1 launch together on the downbeat instead of drifting past
// each other. A grid whose lines do not land on the bar line is not a
// launch grid.
//
// Because they nest, ONE list of lines serves every slot: the clock finds
// every eighth-of-a-bar line and says which STEP of the bar each one is,
// and a division fires on the steps it divides.
//------------------------------------------------------------------------
enum class LaunchDivision
{
	Bar = 0,     ///< 1/1
	HalfBar,     ///< 1/2
	Quarter,     ///< 1/4
	Eighth       ///< 1/8
};

constexpr int kLaunchDivisionCount = 4;

/** The finest division, and so how many steps a bar is cut into. */
constexpr int kGridStepsPerBar = 8;

/** How many of those steps make one of these. */
constexpr int divisionSteps (LaunchDivision division)
{
	return (division == LaunchDivision::Bar)     ? 8
	     : (division == LaunchDivision::HalfBar) ? 4
	     : (division == LaunchDivision::Quarter) ? 2
	                                             : 1;
}

/** Does a slot on this division launch on `step`?

    THE WHOLE SUBDIVISION RULE, in one line. Step 0 is the bar line and
    every division fires on it, which is what keeps a 1/8 slot and a 1/1
    slot in phase with each other. */
constexpr bool divisionFires (LaunchDivision division, int step)
{
	return step >= 0 && (step % divisionSteps (division)) == 0;
}

/** Round-trip helpers for the parameter that carries this. An out of
    range value is a whole bar - the safe, coarsest answer. */
constexpr LaunchDivision divisionFromIndex (int index)
{
	return (index == 1) ? LaunchDivision::HalfBar
	     : (index == 2) ? LaunchDivision::Quarter
	     : (index == 3) ? LaunchDivision::Eighth
	                    : LaunchDivision::Bar;
}

constexpr int indexOfDivision (LaunchDivision division)
{
	return static_cast<int> (division);
}

/** "1/1" .. "1/8", for the panel. Never null. */
const char* divisionShortName (LaunchDivision division);

/** "Bar" .. "1/8 bar", for a host's parameter list and the tooltip. */
const char* divisionName (LaunchDivision division);

//------------------------------------------------------------------------
/** How many grid lines one block can launch on.

    One is the normal answer and two is already exotic - a 512-sample
    block is about 11 ms, and an eighth of a bar is a good fraction of a
    second. The cap exists only so a host reporting a nonsensical tempo
    cannot make this loop for ever; overflowing it costs nothing, because
    what is dropped is the finest lines of a grid nobody could hear at
    that tempo anyway. */
constexpr int kMaxGridLinesPerBlock = 32;

//------------------------------------------------------------------------
/** The host's transport, as much of it as this plug-in needs.

    Three levels of knowledge, and they degrade separately:

      * no context at all - the host gave us nothing. Nothing can be
        gated or quantised, so the plug-in falls back to launching
        immediately, because a sampler that is silent in a host that
        reports no transport looks broken rather than careful;

      * a context but no musical information - we know whether the
        transport is rolling but not where the bars are, so a pad launches
        as soon as the transport rolls;

      * everything - gate on the transport, launch on the bar. */
struct TransportInfo
{
	bool   hasContext = false;
	bool   playing    = false;
	/** Tempo, time signature and musical position are ALL valid. Any one
	    missing and the bars cannot be located, so it is one flag. */
	bool   musical    = false;

	/** The TEMPO ALONE is valid - which is a different question from
	    `musical` and is asked by a different feature. Locating a bar
	    needs all three facts; fitting a sample to the project's tempo
	    needs only this one, and needs it even while the transport is
	    stopped. `tempoBpm` below defaults to 120 so that the bar
	    arithmetic has something workable to divide by, so a caller that
	    must not invent a tempo has to read THIS flag and not that
	    value. */
	bool   tempoKnown = false;

	double tempoBpm       = 120.0;
	int    sigNumerator   = 4;
	int    sigDenominator = 4;

	/** The project position at the START of the block, in quarter notes. */
	double ppq = 0.0;

	/** The bar length in quarter notes: 4/4 is 4, 3/4 is 3, 7/8 is 3.5.
	    Returns 0 for a nonsense signature, which every caller treats as
	    "no bars to find". */
	double barLength () const;
};

/** 0..1 through the current bar, at the start of the block. 0 when the
    bar length is nonsense. */
double barPhase (const TransportInfo& info);

/** Which bar the block starts in, counting from 1 as a musician would.
    Bar 1 begins at quarter note 0. */
long long barNumber (const TransportInfo& info);

//------------------------------------------------------------------------
/** One line of the launch grid inside a block. */
struct GridLine
{
	/** Where in the block, in samples. 0 means the block BEGINS on it. */
	int offset = 0;
	/** Which step of the bar, 0 .. kGridStepsPerBar - 1. Step 0 is the
	    bar line. Pass it to divisionFires. */
	int step = 0;
};

//------------------------------------------------------------------------
/** Finds the grid lines inside a block, once each.

    Holds the small amount of memory that "once each" requires: which bar
    line was last fired, and where the previous block ended. Both are
    thrown away by reset(), which the processor calls whenever the
    transport stops - so rewinding to the top of a bar and pressing play
    launches on that bar rather than skipping it because it was fired
    before the stop. */
class BarClock
{
public:
	void reset ();

	/** Writes the grid lines inside this block, in order, and returns how
	    many. Zero when the transport is not rolling, when the musical
	    information is missing, or when no line falls inside this block -
	    which is the usual answer.

	    EVERY line is written, at the finest division, with its step. A
	    caller acts on the ones its own division fires on; that is one
	    list rather than four, and it is only possible because the
	    divisions nest.

	    An offset of 0 means the block BEGINS on a line. That is a real
	    case, not an edge case: it is what happens every time a host
	    starts playback from the top of a bar. */
	int gridLinesInBlock (const TransportInfo& info, int numSamples, double sampleRate,
	                      GridLine* lines, int maxLines);

private:
	/** The musical position of the last line fired, so the same line
	    cannot be fired twice by two blocks that both contain it. */
	double mLastFiredLine = kNever;
	/** Where the previous block STARTED, so a jump BACKWARDS - a locate,
	    or a cycle wrapping - can be told from ordinary forward progress
	    and can clear the guard above. Without this a one-bar cycle fires
	    its bar line once and never again.

	    The previous START and not the previous end, deliberately. A block
	    handed the same position twice is not a jump: its start equals the
	    last one rather than preceding it, so the guard holds and the line
	    is not fired a second time. Measured against the previous END,
	    every repeated block would look like a locate. */
	double mPreviousStartPpq = kNever;
	bool   mHavePrevious = false;

	static constexpr double kNever = -1.0e18;
};

//------------------------------------------------------------------------
} // namespace Project6
