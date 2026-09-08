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
	section ("3. Finding the line inside a block");
	//--------------------------------------------------------------------
	{
		int offsets[kMaxBarLinesPerBlock];

		// A BLOCK THAT BEGINS ON A BAR LINE. Not an edge case - it is what
		// happens every time a host starts playback from the top of a bar.
		{
			BarClock clock;
			const int n = clock.barLinesInBlock (rolling (4.0), 512, kRate,
			                                     offsets, kMaxBarLinesPerBlock);
			check (n == 1, "a block starting exactly on a bar line finds it");
			check (n == 1 && offsets[0] == 0, "at offset 0");
		}

		// A line part way through.
		{
			BarClock clock;
			// Start a quarter of a beat before bar 2: 12000 samples.
			const int n = clock.barLinesInBlock (rolling (4.0 - 0.5), 24000, kRate,
			                                     offsets, kMaxBarLinesPerBlock);
			check (n == 1, "a line inside the block is found");
			check (n == 1 && offsets[0] == 12000,
			       "at the sample it actually falls on, not at the block edge");
		}

		// No line at all - the usual answer.
		{
			BarClock clock;
			const int n = clock.barLinesInBlock (rolling (1.0), 512, kRate,
			                                     offsets, kMaxBarLinesPerBlock);
			check (n == 0, "a block in the middle of a bar finds nothing");
		}

		// The line at the very END of a block belongs to the NEXT block.
		// Firing it here would launch a pad one block early, every time.
		{
			BarClock clock;
			// At 120 BPM and 48 kHz a quarter note is 24000 samples, so
			// 12000 from 3.5 ends exactly on quarter note 4.0.
			const int n = clock.barLinesInBlock (rolling (3.5), 12000, kRate,
			                                     offsets, kMaxBarLinesPerBlock);
			check (n == 0, "a line exactly at the end of a block is not this block's");

			const int next = clock.barLinesInBlock (rolling (4.0), 512, kRate,
			                                        offsets, kMaxBarLinesPerBlock);
			check (next == 1 && offsets[0] == 0, "it belongs to the next one, at offset 0");
		}

		// Two lines in one block: a very short bar and a very fast tempo.
		{
			BarClock clock;
			// 1/4 time at 240 BPM: a bar is one quarter note, 12000
			// samples at 48 k. A 32000-sample block spans two lines.
			const int n = clock.barLinesInBlock (rolling (0.0, 1, 4, 240.0), 32000, kRate,
			                                     offsets, kMaxBarLinesPerBlock);
			check (n == 3, "three bar lines in one long block, at this tempo");
			check (n == 3 && offsets[0] == 0 && offsets[1] == 12000 && offsets[2] == 24000,
			       "in order, at the right samples");
		}

		// Odd time signature, so the arithmetic is not silently 4/4.
		{
			BarClock clock;
			// 7/8 at 120 BPM: a bar is 3.5 quarter notes = 84000 samples.
			// Start half a beat before bar 2, which is at 3.5.
			const int n = clock.barLinesInBlock (rolling (3.0, 7, 8), 48000, kRate,
			                                     offsets, kMaxBarLinesPerBlock);
			check (n == 1 && offsets[0] == 12000, "7/8 puts its line at 3.5 quarter notes");
		}
	}

	//--------------------------------------------------------------------
	section ("4. Once each, and once again after a jump");
	//--------------------------------------------------------------------
	{
		int offsets[kMaxBarLinesPerBlock];

		// THE SAME LINE MUST NOT FIRE TWICE.
		//
		// A repeated block - the same position handed over twice, which
		// some hosts do while scrubbing - is not a locate, and firing on
		// it would be wrong even though applying a bar line is idempotent
		// (see Project6Processor: it only acts on slots whose armed state
		// differs from what is launched). This is the case that made the
		// backwards-jump test compare against the previous block's START
		// rather than its end.
		{
			BarClock clock;
			check (clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets,
			                              kMaxBarLinesPerBlock) == 1,
			       "the line fires on the block that contains it");
			check (clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets,
			                              kMaxBarLinesPerBlock) == 0,
			       "NEGATIVE CONTROL: and not again on a repeat of the same block");
		}

		// Ordinary forward progress across many blocks fires each line
		// exactly once - a whole 4/4 bar in 512-sample blocks.
		{
			BarClock clock;
			int fired = 0;
			double ppq = 4.0;
			const double perBlock = 512.0 * (120.0 / 60.0) / kRate;   // quarter notes
			for (int block = 0; block < 400; ++block)
			{
				fired += clock.barLinesInBlock (rolling (ppq), 512, kRate, offsets,
				                                kMaxBarLinesPerBlock);
				ppq += perBlock;
			}
			// 400 blocks * 512 samples = 204800 samples = 2.13 bars, from
			// a bar line: lines at 4, 8 and 12 quarter notes.
			check (fired == 3, "three lines in two and a bit bars, each fired once");
		}

		// A CYCLE THAT WRAPS. One-bar loop: the same line arrives again
		// and again, and must launch again and again. This is the case
		// the previous-block-end memory exists for; without it a one-bar
		// loop works exactly once.
		{
			BarClock clock;
			int fired = 0;
			for (int pass = 0; pass < 4; ++pass)
			{
				// The block at the top of the bar...
				fired += clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets,
				                                kMaxBarLinesPerBlock);
				// ...then somewhere in the middle...
				clock.barLinesInBlock (rolling (6.0), 512, kRate, offsets,
				                       kMaxBarLinesPerBlock);
				// ...then the cycle jumps back.
			}
			check (fired == 4, "a one-bar cycle fires its line on every pass");
		}

		// A LOCATE BACKWARDS to an earlier bar fires that bar again.
		{
			BarClock clock;
			clock.barLinesInBlock (rolling (16.0), 512, kRate, offsets, kMaxBarLinesPerBlock);
			const int n = clock.barLinesInBlock (rolling (8.0), 512, kRate, offsets,
			                                     kMaxBarLinesPerBlock);
			check (n == 1, "locating back to an earlier bar fires it");
		}

		// STOP AND PLAY FROM THE SAME PLACE. reset() is what the processor
		// calls on a transport stop; without it, rewinding to the bar you
		// just played and pressing play launches nothing.
		{
			BarClock clock;
			check (clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets,
			                              kMaxBarLinesPerBlock) == 1, "played once");
			clock.reset ();
			check (clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets,
			                              kMaxBarLinesPerBlock) == 1,
			       "and again after a stop, from the same position");
		}
	}

	//--------------------------------------------------------------------
	section ("5. When the host tells us nothing useful");
	//--------------------------------------------------------------------
	{
		int offsets[kMaxBarLinesPerBlock];
		BarClock clock;

		TransportInfo stopped = rolling (4.0);
		stopped.playing = false;
		check (clock.barLinesInBlock (stopped, 512, kRate, offsets, kMaxBarLinesPerBlock) == 0,
		       "a stopped transport has no bar lines");

		TransportInfo unmusical = rolling (4.0);
		unmusical.musical = false;
		check (clock.barLinesInBlock (unmusical, 512, kRate, offsets, kMaxBarLinesPerBlock) == 0,
		       "and neither has one with no tempo or signature");

		check (clock.barLinesInBlock (rolling (4.0), 0, kRate, offsets,
		                              kMaxBarLinesPerBlock) == 0,
		       "a zero-length block finds nothing");
		check (clock.barLinesInBlock (rolling (4.0), -8, kRate, offsets,
		                              kMaxBarLinesPerBlock) == 0,
		       "nor a negative one");
		check (clock.barLinesInBlock (rolling (4.0), 512, 0.0, offsets,
		                              kMaxBarLinesPerBlock) == 0,
		       "nor a zero sample rate");

		check (clock.barLinesInBlock (rolling (4.0, 0, 4), 512, kRate, offsets,
		                              kMaxBarLinesPerBlock) == 0,
		       "nor a nonsense time signature");
		check (clock.barLinesInBlock (rolling (4.0, 4, 4, 0.0), 512, kRate, offsets,
		                              kMaxBarLinesPerBlock) == 0,
		       "nor a tempo of zero");
		check (clock.barLinesInBlock (rolling (4.0, 4, 4, -120.0), 512, kRate, offsets,
		                              kMaxBarLinesPerBlock) == 0,
		       "nor a negative one");

		check (clock.barLinesInBlock (rolling (4.0), 512, kRate, nullptr,
		                              kMaxBarLinesPerBlock) == 0,
		       "and a null buffer is refused rather than written to");
		check (clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets, 0) == 0,
		       "as is a buffer with no room");

		// A gap in the musical information must not leave the guard
		// measuring against a position from before it.
		check (clock.barLinesInBlock (rolling (4.0), 512, kRate, offsets,
		                              kMaxBarLinesPerBlock) == 1,
		       "and the clock still works once the host makes sense again");
	}

	//--------------------------------------------------------------------
	section ("6. The cap, which nothing normal reaches");
	//--------------------------------------------------------------------
	{
		int offsets[kMaxBarLinesPerBlock];
		BarClock clock;

		// 1/4 at 999 BPM over a huge block: far more lines than the cap.
		// Overflowing must return the cap and stop, not run off the end -
		// and every line does the same thing anyway, so the ones dropped
		// cost nothing.
		const int n = clock.barLinesInBlock (rolling (0.0, 1, 4, 999.0), 48000, kRate,
		                                     offsets, kMaxBarLinesPerBlock);
		check (n == kMaxBarLinesPerBlock, "an absurd tempo fills the buffer and stops");
		bool ordered = true;
		for (int i = 1; i < n; ++i)
			ordered &= (offsets[i] > offsets[i - 1]);
		check (ordered, "and what it wrote is still in order");
		bool inRange = true;
		for (int i = 0; i < n; ++i)
			inRange &= (offsets[i] >= 0 && offsets[i] < 48000);
		check (inRange, "and inside the block");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
