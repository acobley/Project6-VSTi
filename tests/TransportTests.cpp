//------------------------------------------------------------------------
// Project6 - bar clock tests
//
// SDK-FREE.
//
//     c++ -std=c++17 -O2 -Wall -Isource tests/TransportTests.cpp
//         source/Project6Transport.cpp -o /tmp/transporttests
//         && /tmp/transporttests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//
// This suite exists because every way of getting bar detection wrong is
// SILENT. A line fired twice launches a pad and relaunches it a sample
// later; a line never fired again leaves a cycle that works once; a bar
// length that assumes 4/4 is right in most music and wrong in the rest.
// None of them crash, and all of them are one number.
//------------------------------------------------------------------------

#include "Project6Transport.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Project6;

namespace {

int gFailures = 0;

void check (bool condition, const char* what)
{
	std::printf ("  %-64s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++gFailures;
}

void section (const char* title)
{
	std::printf ("\n%s\n", title);
}

bool close (double a, double b, double tolerance)
{
	return std::fabs (a - b) <= tolerance;
}

/** 120 BPM at 48 kHz: one quarter note is half a second, 24000 samples.
    A 4/4 bar is 96000 samples. Round numbers on purpose - an assertion
    about a sample offset should be readable. */
constexpr double kRate = 48000.0;

TransportInfo rolling (double ppq, int numerator = 4, int denominator = 4,
                       double tempo = 120.0)
{
	TransportInfo info;
	info.hasContext     = true;
	info.playing        = true;
	info.musical        = true;
	info.tempoBpm       = tempo;
	info.sigNumerator   = numerator;
	info.sigDenominator = denominator;
	info.ppq            = ppq;
	return info;
}

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - bar clock tests\n");

	//--------------------------------------------------------------------
	section ("1. How long a bar is");
	//--------------------------------------------------------------------
	{
		check (close (rolling (0.0, 4, 4).barLength (), 4.0, 1e-12), "4/4 is four quarter notes");
		check (close (rolling (0.0, 3, 4).barLength (), 3.0, 1e-12), "3/4 is three");
		check (close (rolling (0.0, 6, 8).barLength (), 3.0, 1e-12), "6/8 is also three");
		check (close (rolling (0.0, 7, 8).barLength (), 3.5, 1e-12), "7/8 is three and a half");
		check (close (rolling (0.0, 5, 4).barLength (), 5.0, 1e-12), "5/4 is five");
		check (close (rolling (0.0, 2, 2).barLength (), 4.0, 1e-12), "cut common is four");

		// NEGATIVE CONTROL for the whole idea. If bar length were hard
		// coded to four, 7/8 would come back as four and every bar line
		// in the piece would be in the wrong place.
		check (! close (rolling (0.0, 7, 8).barLength (), 4.0, 1e-12),
		       "NEGATIVE CONTROL: 7/8 is not four quarter notes");

		TransportInfo nonsense = rolling (0.0, 0, 4);
		check (nonsense.barLength () == 0.0, "a zero numerator has no bar length");
		nonsense = rolling (0.0, 4, 0);
		check (nonsense.barLength () == 0.0, "and neither has a zero denominator");
	}

	//--------------------------------------------------------------------
	section ("2. Where in the bar, and which bar");
	//--------------------------------------------------------------------
	{
		check (close (barPhase (rolling (0.0)),  0.0,  1e-12), "the top of bar 1 is phase 0");
		check (close (barPhase (rolling (2.0)),  0.5,  1e-12), "beat 3 of 4/4 is half way");
		check (close (barPhase (rolling (7.0)),  0.75, 1e-12), "and beat 4 of bar 2 is three quarters");
		check (close (barPhase (rolling (1.0, 7, 8)), 1.0 / 3.5, 1e-12), "7/8 divides by 3.5");

		check (barNumber (rolling (0.0)) == 1, "quarter note 0 is bar 1, as a musician counts");
		check (barNumber (rolling (3.99)) == 1, "just before the line is still bar 1");
		check (barNumber (rolling (4.0)) == 2, "and the line itself is bar 2");
		check (barNumber (rolling (40.0)) == 11, "ten bars in is bar 11");
		check (barNumber (rolling (0.0, 3, 4)) == 1, "3/4 counts from 1 too");
		check (barNumber (rolling (3.0, 3, 4)) == 2, "with shorter bars");

		// Pre-roll: a host can hand out negative positions.
		check (barNumber (rolling (-1.0)) == 0, "a bar before the start is bar 0");
		check (close (barPhase (rolling (-1.0)), 0.75, 1e-12),
		       "and its phase is still inside 0..1");
	}

	//--------------------------------------------------------------------
	section ("3. Finding the lines inside a block");
	//--------------------------------------------------------------------
	{
		GridLine lines[kMaxGridLinesPerBlock];

		// At 120 BPM and 48 kHz a quarter note is 24000 samples, a 4/4 bar
		// is 96000, and one step of the grid - an eighth of a bar - is
		// 12000. Round numbers on purpose: an assertion about a sample
		// offset should be readable.

		// A BLOCK THAT BEGINS ON A BAR LINE. Not an edge case - it is what
		// happens every time a host starts playback from the top of a bar.
		{
			BarClock clock;
			const int n = clock.gridLinesInBlock (rolling (4.0), 512, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n == 1, "a block starting exactly on a line finds it, and only it");
			check (n == 1 && lines[0].offset == 0, "at offset 0");
			check (n == 1 && lines[0].step == 0, "and it is step 0 - the bar line");
		}

		// A line part way through.
		{
			BarClock clock;
			// 240 samples before the bar line at quarter note 4.
			const int n = clock.gridLinesInBlock (rolling (4.0 - 0.01), 512, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n == 1, "a line inside the block is found");
			check (n == 1 && lines[0].offset == 240,
			       "at the sample it actually falls on, not at the block edge");
		}

		// No line at all - still the usual answer, even at eight steps a
		// bar.
		{
			BarClock clock;
			const int n = clock.gridLinesInBlock (rolling (4.05), 512, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n == 0, "a block between two lines finds nothing");
		}

		// The line at the very END of a block belongs to the NEXT block.
		// Firing it here would launch a pad one block early, every time.
		{
			BarClock clock;
			// 9600 samples from 3.6 ends exactly on quarter note 4.0.
			const int n = clock.gridLinesInBlock (rolling (3.6), 9600, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n == 0, "a line exactly at the end of a block is not this block's");

			const int next = clock.gridLinesInBlock (rolling (4.0), 512, kRate,
			                                         lines, kMaxGridLinesPerBlock);
			check (next == 1 && lines[0].offset == 0,
			       "it belongs to the next one, at offset 0");
		}

		// TWO LINES IN ONE BLOCK, which the subdivided grid makes ordinary
		// rather than exotic.
		{
			BarClock clock;
			// 24000 samples from 3.99: spans the bar line at 4.0 and the
			// 1/8 line at 4.5.
			const int n = clock.gridLinesInBlock (rolling (3.99), 24000, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n == 2, "a block spanning two lines finds both");
			check (n == 2 && lines[0].offset == 240 && lines[1].offset == 12240,
			       "in order, at the right samples");
			check (n == 2 && lines[0].step == 0 && lines[1].step == 1,
			       "and step 0 is followed by step 1");
		}

		// THE STEPS RUN 0..7 AND WRAP. Step 0 is the bar line, and it has
		// to keep being the bar line however far into the project we are.
		{
			BarClock clock;
			// Ten whole bars in, plus five steps.
			const int n = clock.gridLinesInBlock (rolling (40.0 + 2.5 - 0.01), 512, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n == 1 && lines[0].step == 5,
			       "a line two and a half beats into bar 11 is step 5");
		}

		// Odd time signature, so the arithmetic is not silently 4/4. A 7/8
		// bar is 3.5 quarter notes, so a step is 0.4375 - 10500 samples.
		{
			BarClock clock;
			const int n = clock.gridLinesInBlock (rolling (3.0, 7, 8), 24000, kRate,
			                                      lines, kMaxGridLinesPerBlock);
			check (n >= 2, "7/8 has its own step length, and finds lines by it");
			check (n >= 2 && lines[0].offset == 1500 && lines[0].step == 7,
			       "the last step of the bar comes first here");
			check (n >= 2 && lines[1].offset == 12000 && lines[1].step == 0,
			       "and the bar line at 3.5 quarter notes is step 0");

			// NEGATIVE CONTROL for the whole nesting idea: the bar line of
			// a 7/8 bar is a grid line, so a slot on 1/1 and a slot on 1/8
			// launch together there.
			check (n >= 2 && divisionFires (LaunchDivision::Bar, lines[1].step)
			           && divisionFires (LaunchDivision::Eighth, lines[1].step),
			       "NEGATIVE CONTROL: every division fires on it, even in 7/8");
		}
	}

	//--------------------------------------------------------------------
	section ("4. Once each, and once again after a jump");
	//--------------------------------------------------------------------
	{
		GridLine lines[kMaxGridLinesPerBlock];

		// THE SAME LINE MUST NOT FIRE TWICE.
		//
		// A repeated block - the same position handed over twice, which
		// some hosts do while scrubbing - is not a locate, and firing on
		// it would be wrong even though applying a grid line is idempotent
		// (see Project6Processor: it only acts on slots whose armed state
		// differs from what is launched). This is the case that made the
		// backwards-jump test compare against the previous block's START
		// rather than its end.
		{
			BarClock clock;
			check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines,
			                               kMaxGridLinesPerBlock) == 1,
			       "the line fires on the block that contains it");
			check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines,
			                               kMaxGridLinesPerBlock) == 0,
			       "NEGATIVE CONTROL: and not again on a repeat of the same block");
		}

		// Ordinary forward progress across many blocks fires each line
		// exactly once.
		{
			BarClock clock;
			int fired = 0;
			double ppq = 4.0;
			const double perBlock = 512.0 * (120.0 / 60.0) / kRate;   // quarter notes
			for (int block = 0; block < 400; ++block)
			{
				fired += clock.gridLinesInBlock (rolling (ppq), 512, kRate, lines,
				                                 kMaxGridLinesPerBlock);
				ppq += perBlock;
			}
			// 400 blocks * 512 samples is 8.533 quarter notes from a bar
			// line: lines at 4.0, 4.5 ... 12.5.
			check (fired == 18, "eighteen lines in eight and a half beats, each fired once");
		}

		// A CYCLE THAT WRAPS. One-bar loop: the same line arrives again
		// and again, and must launch again and again. This is the case
		// the previous-block-start memory exists for; without it a
		// one-bar loop works exactly once.
		{
			BarClock clock;
			int fired = 0;
			for (int pass = 0; pass < 4; ++pass)
			{
				fired += clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines,
				                                 kMaxGridLinesPerBlock);
				clock.gridLinesInBlock (rolling (6.0), 512, kRate, lines,
				                        kMaxGridLinesPerBlock);
			}
			check (fired == 4, "a one-bar cycle fires its bar line on every pass");
		}

		// A LOCATE BACKWARDS to an earlier bar fires that bar again.
		{
			BarClock clock;
			clock.gridLinesInBlock (rolling (16.0), 512, kRate, lines,
			                        kMaxGridLinesPerBlock);
			const int n = clock.gridLinesInBlock (rolling (8.0), 512, kRate, lines,
			                                      kMaxGridLinesPerBlock);
			check (n == 1, "locating back to an earlier bar fires it");
		}

		// STOP AND PLAY FROM THE SAME PLACE. reset() is what the processor
		// calls on a transport stop; without it, rewinding to the bar you
		// just played and pressing play launches nothing.
		{
			BarClock clock;
			check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines,
			                               kMaxGridLinesPerBlock) == 1, "played once");
			clock.reset ();
			check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines,
			                               kMaxGridLinesPerBlock) == 1,
			       "and again after a stop, from the same position");
		}
	}

	//--------------------------------------------------------------------
	section ("5. When the host tells us nothing useful");
	//--------------------------------------------------------------------
	{
		GridLine lines[kMaxGridLinesPerBlock];
		BarClock clock;

		TransportInfo stopped = rolling (4.0);
		stopped.playing = false;
		check (clock.gridLinesInBlock (stopped, 512, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "a stopped transport has no grid lines");

		TransportInfo unmusical = rolling (4.0);
		unmusical.musical = false;
		check (clock.gridLinesInBlock (unmusical, 512, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "and neither has one with no tempo or signature");

		check (clock.gridLinesInBlock (rolling (4.0), 0, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "a zero-length block finds nothing");
		check (clock.gridLinesInBlock (rolling (4.0), -8, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "nor a negative one");
		check (clock.gridLinesInBlock (rolling (4.0), 512, 0.0, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "nor a zero sample rate");

		check (clock.gridLinesInBlock (rolling (4.0, 0, 4), 512, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "nor a nonsense time signature");
		check (clock.gridLinesInBlock (rolling (4.0, 4, 4, 0.0), 512, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "nor a tempo of zero");
		check (clock.gridLinesInBlock (rolling (4.0, 4, 4, -120.0), 512, kRate, lines,
		                               kMaxGridLinesPerBlock) == 0,
		       "nor a negative one");

		check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, nullptr,
		                               kMaxGridLinesPerBlock) == 0,
		       "and a null buffer is refused rather than written to");
		check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines, 0) == 0,
		       "as is a buffer with no room");

		// A gap in the musical information must not leave the guard
		// measuring against a position from before it.
		check (clock.gridLinesInBlock (rolling (4.0), 512, kRate, lines,
		                               kMaxGridLinesPerBlock) == 1,
		       "and the clock still works once the host makes sense again");
	}

	//--------------------------------------------------------------------
	section ("6. The cap, which nothing normal reaches");
	//--------------------------------------------------------------------
	{
		GridLine lines[kMaxGridLinesPerBlock];
		BarClock clock;

		// 1/4 at 999 BPM over a huge block: far more lines than the cap.
		// Overflowing must return the cap and stop, not run off the end.
		const int n = clock.gridLinesInBlock (rolling (0.0, 1, 4, 999.0), 48000, kRate,
		                                      lines, kMaxGridLinesPerBlock);
		check (n == kMaxGridLinesPerBlock, "an absurd tempo fills the buffer and stops");
		bool ordered = true, inRange = true, stepped = true;
		for (int i = 0; i < n; ++i)
		{
			if (i > 0)
				ordered &= (lines[i].offset >= lines[i - 1].offset);
			inRange &= (lines[i].offset >= 0 && lines[i].offset < 48000);
			stepped &= (lines[i].step >= 0 && lines[i].step < kGridStepsPerBar);
		}
		check (ordered, "and what it wrote is still in order");
		check (inRange, "and inside the block");
		check (stepped, "with every step inside the bar");
	}

	//--------------------------------------------------------------------
	section ("7. The divisions, and how they nest");
	//--------------------------------------------------------------------
	{
		check (kGridStepsPerBar == 8, "the grid is eight steps to the bar");
		check (kLaunchDivisionCount == 4, "and there are four divisions to choose from");

		check (divisionSteps (LaunchDivision::Bar)     == 8, "a bar is eight steps");
		check (divisionSteps (LaunchDivision::HalfBar) == 4, "a half bar is four");
		check (divisionSteps (LaunchDivision::Quarter) == 2, "a quarter is two");
		check (divisionSteps (LaunchDivision::Eighth)  == 1, "and an eighth is one");

		// EVERY DIVISION FIRES ON STEP 0. That is what keeps a slot on
		// 1/8 and a slot on 1/1 in phase with each other instead of
		// drifting apart, and it is the reason one list of lines serves
		// them all.
		bool allOnDownbeat = true;
		for (int i = 0; i < kLaunchDivisionCount; ++i)
			allOnDownbeat &= divisionFires (divisionFromIndex (i), 0);
		check (allOnDownbeat, "every division fires on the bar line");

		// And they NEST: anything a coarser division fires on, a finer one
		// fires on too.
		bool nests = true;
		for (int step = 0; step < kGridStepsPerBar; ++step)
		{
			if (divisionFires (LaunchDivision::Bar, step))
				nests &= divisionFires (LaunchDivision::HalfBar, step);
			if (divisionFires (LaunchDivision::HalfBar, step))
				nests &= divisionFires (LaunchDivision::Quarter, step);
			if (divisionFires (LaunchDivision::Quarter, step))
				nests &= divisionFires (LaunchDivision::Eighth, step);
		}
		check (nests, "and a finer division fires everywhere a coarser one does");

		// The exact steps, spelt out - this is the behaviour, not an
		// implementation detail.
		int barCount = 0, halfCount = 0, quarterCount = 0, eighthCount = 0;
		for (int step = 0; step < kGridStepsPerBar; ++step)
		{
			barCount     += divisionFires (LaunchDivision::Bar, step)     ? 1 : 0;
			halfCount    += divisionFires (LaunchDivision::HalfBar, step) ? 1 : 0;
			quarterCount += divisionFires (LaunchDivision::Quarter, step) ? 1 : 0;
			eighthCount  += divisionFires (LaunchDivision::Eighth, step)  ? 1 : 0;
		}
		check (barCount == 1,     "a bar slot launches once a bar");
		check (halfCount == 2,    "a half-bar slot twice");
		check (quarterCount == 4, "a quarter four times");
		check (eighthCount == 8,  "and an eighth eight times");

		check (! divisionFires (LaunchDivision::Bar, 4),
		       "NEGATIVE CONTROL: a bar slot does NOT launch half way through");
		check (divisionFires (LaunchDivision::HalfBar, 4),
		       "but a half-bar slot does");
		check (! divisionFires (LaunchDivision::HalfBar, 2),
		       "and a half-bar slot does not launch on the quarter");

		check (! divisionFires (LaunchDivision::Eighth, -1),
		       "a negative step fires nothing");

		// Round-tripping, and what an out-of-range value means.
		bool roundTrips = true;
		for (int i = 0; i < kLaunchDivisionCount; ++i)
			roundTrips &= (indexOfDivision (divisionFromIndex (i)) == i);
		check (roundTrips, "index -> division -> index round-trips");
		check (divisionFromIndex (-1) == LaunchDivision::Bar,
		       "a nonsense index is a whole bar - the coarsest, safest answer");
		check (divisionFromIndex (99) == LaunchDivision::Bar, "at either end");

		// Names, which the panel and the host both show.
		check (std::string (divisionShortName (LaunchDivision::Bar)) == "1/1",
		       "the panel reads 1/1");
		check (std::string (divisionShortName (LaunchDivision::Eighth)) == "1/8",
		       "through to 1/8");
		check (std::string (divisionName (LaunchDivision::Bar)) == "Bar",
		       "and a host reads Bar");
		check (std::string (divisionName (LaunchDivision::HalfBar)) == "1/2 bar",
		       "through to 1/2 bar");

		bool named = true;
		for (int i = 0; i < kLaunchDivisionCount; ++i)
		{
			const LaunchDivision d = divisionFromIndex (i);
			named &= (divisionShortName (d) != nullptr && divisionShortName (d)[0] != '\0');
			named &= (divisionName (d) != nullptr && divisionName (d)[0] != '\0');
		}
		check (named, "and every division has both names");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
