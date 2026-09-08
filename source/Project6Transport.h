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

/** How many bar lines one block can launch on.

    One is the normal answer and two is already exotic - a 512-sample block
    is about 11 ms, and a bar is the best part of a second. The cap exists
    only so a host reporting a nonsensical tempo cannot make this loop for
    ever; overflowing it costs nothing, because every bar line does the
    same thing and doing it once is enough. */
constexpr int kMaxBarLinesPerBlock = 16;

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
/** Finds the bar lines inside a block, once each.

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

	/** Writes the sample offsets at which a bar begins, in order, and
	    returns how many. Zero when the transport is not rolling, when the
	    musical information is missing, or when no bar line falls inside
	    this block - which is the usual answer.

	    An offset of 0 means the block BEGINS on a bar line. That is a
	    real case, not an edge case: it is what happens every time a host
	    starts playback from the top of a bar. */
	int barLinesInBlock (const TransportInfo& info, int numSamples, double sampleRate,
	                     int* offsets, int maxOffsets);

private:
	/** The musical position of the last bar line fired, so the same line
	    cannot be fired twice by two blocks that both contain it. */
	double mLastFiredBar = kNever;
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
