//------------------------------------------------------------------------
// Project6 - DSP tests
//
// SDK-FREE, deliberately: Project6Dsp.{h,cpp} include no VST3 header, so
// this suite compiles and runs anywhere with
//
//     c++ -std=c++17 -O2 -Isource tests/DspTests.cpp
//         source/Project6Dsp.cpp -o /tmp/dsptests && /tmp/dsptests
//
//   (one line; it is broken here only because a comment line may not end
//    in a backslash - the compiler reads that as a line continuation and
//    warns, which is exactly the kind of noise a clean build must not have)
//
// It is the only integration test the project has until the plug-in is
// built and put through the validator, so EVERY ASSERTION HERE SHOULD BE
// ONE THAT WOULD FAIL AGAINST A WRONG IMPLEMENTATION. Section 2's negative
// control is there because a guard that has never failed is a guess.
//
// Nothing is synthesised yet, so what is under test is the output stage
// and the mappings around it. That is not a small thing to have covered:
// it is the stage every sample eventually passes through, and the one that
// will be hardest to notice going wrong once there is audio to listen to.
//------------------------------------------------------------------------

#include "Project6Dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Project6;

namespace {

int gFailures = 0;

void check (bool condition, const char* what)
{
	std::printf ("  %-62s %s\n", what, condition ? "ok" : "FAILED");
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

/** A deterministic, full-scale-ish test signal. Not silence, and not a
    constant: a constant would hide a gain applied to only one channel,
    and silence would hide everything. */
std::vector<float> testSignal (int frames)
{
	std::vector<float> buffer (static_cast<size_t> (frames) * kChannelCount);
	for (int i = 0; i < frames; ++i)
	{
		const double t = i / 64.0;
		buffer[static_cast<size_t> (i) * 2]     = static_cast<float> (0.9 * std::sin (t));
		buffer[static_cast<size_t> (i) * 2 + 1] = static_cast<float> (0.9 * std::cos (t * 0.7));
	}
	return buffer;
}

double peakOf (const std::vector<float>& buffer)
{
	double peak = 0.0;
	for (float v : buffer)
		peak = std::max (peak, static_cast<double> (std::fabs (v)));
	return peak;
}

/** Settle the smoother at its current target, so a later assertion is
    about the ramp being tested and not about the first-block snap. */
void settle (Project6Dsp& dsp, int frames = 4096)
{
	std::vector<float> scratch (static_cast<size_t> (frames) * kChannelCount, 0.f);
	dsp.applyOutputTrim (scratch.data (), frames);
}

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - DSP tests\n");

	//--------------------------------------------------------------------
	section ("1. The decibel mapping, at both ends and in the middle");
	//--------------------------------------------------------------------
	{
		// The top of travel is UNITY. If this ever reads above 1.0 the
		// default patch has a boost in it that nothing else will mention.
		check (close (dbToLinear (kTrimMaxDb, kTrimMinDb), 1.0, 1e-12),
		       "0 dB is unity gain");

		// The bottom is SILENCE, not 10^(-60/20). A slider pulled all the
		// way down must be off.
		check (dbToLinear (kTrimMinDb, kTrimMinDb) == 0.0,
		       "the bottom of travel is exactly zero, not -60 dB of leakage");
		check (dbToLinear (kTrimMinDb - 20.0, kTrimMinDb) == 0.0,
		       "below the bottom of travel is still exactly zero");

		check (close (dbToLinear (-6.020599913279624, kTrimMinDb), 0.5, 1e-9),
		       "-6.0206 dB is half amplitude");
		check (close (dbToLinear (-20.0, kTrimMinDb), 0.1, 1e-12),
		       "-20 dB is one tenth");

		// The inverse, so a measurement can be reported in the panel's
		// own units.
		check (close (linearToDb (1.0, kTrimMinDb), 0.0, 1e-12),
		       "unity reads back as 0 dB");
		check (close (linearToDb (0.5, kTrimMinDb), -6.020599913279624, 1e-9),
		       "half amplitude reads back as -6.0206 dB");
		check (linearToDb (0.0, kTrimMinDb) == kTrimMinDb,
		       "silence reads back as the floor, not as -inf");

		// Round trip. The two are used on opposite sides of the
		// processor / controller split, so they have to agree exactly and
		// not merely nearly.
		bool roundTrips = true;
		for (double db = kTrimMinDb + 0.5; db <= kTrimMaxDb; db += 0.5)
			roundTrips &= close (linearToDb (dbToLinear (db, kTrimMinDb), kTrimMinDb), db, 1e-9);
		check (roundTrips, "dB -> linear -> dB round-trips across the whole range");
	}

	//--------------------------------------------------------------------
	section ("2. Unity is BIT-IDENTICAL, and the negative control");
	//--------------------------------------------------------------------
	{
		const int frames = 512;
		const std::vector<float> input = testSignal (frames);

		Project6Dsp dsp;
		dsp.setSampleRate (44100.0);
		dsp.setOutputTrimDb (kTrimMaxDb);          // unity

		std::vector<float> output = input;
		dsp.applyOutputTrim (output.data (), frames);

		// Not "close to": IDENTICAL. A trim at the top of its travel must
		// not be a multiply that rounds, or every sample of a bypassed
		// path is one ulp away from the signal that went in.
		check (output == input,
		       "the output stage at 0 dB is bit-identical to its input");

		//----------------------------------------------------------------
		// THE NEGATIVE CONTROL. A guard that has never failed is a guess:
		// if `output == input` were vacuously true - a comparison against
		// the wrong buffer, say, or a stage that never ran - the check
		// above would pass for a broken implementation. So run the same
		// comparison somewhere it MUST fail.
		//----------------------------------------------------------------
		Project6Dsp half;
		half.setSampleRate (44100.0);
		half.setOutputTrimDb (-6.020599913279624);

		std::vector<float> halved = input;
		half.applyOutputTrim (halved.data (), frames);

		check (! (halved == input),
		       "NEGATIVE CONTROL: at -6 dB the same comparison fails");

		double worst = 0.0;
		for (size_t i = 0; i < input.size (); ++i)
			worst = std::max (worst, std::fabs (static_cast<double> (halved[i])
			                                    - static_cast<double> (input[i]) * 0.5));
		check (worst < 1e-6, "and -6.0206 dB really is half amplitude, sample by sample");
	}

	//--------------------------------------------------------------------
	section ("3. The smoother, and what it must never do");
	//--------------------------------------------------------------------
	{
		const int frames = 8192;

		Project6Dsp dsp;
		dsp.setSampleRate (44100.0);
		dsp.setOutputTrimDb (kTrimMaxDb);
		settle (dsp);                               // now sitting at unity

		// A constant input, so the OUTPUT IS THE GAIN and the step from
		// one sample to the next can be read straight off it.
		std::vector<float> buffer (static_cast<size_t> (frames) * kChannelCount, 1.f);
		dsp.setOutputTrimDb (kTrimMinDb);           // ask for silence, hard
		dsp.applyOutputTrim (buffer.data (), frames);

		double biggestStep = 0.0;
		for (int i = 1; i < frames; ++i)
			biggestStep = std::max (biggestStep,
			                        std::fabs (static_cast<double> (buffer[static_cast<size_t> (i) * 2])
			                                   - buffer[static_cast<size_t> (i - 1) * 2]));

		std::printf ("     largest single-sample step: %.5f (%.3f %%)\n",
		             biggestStep, biggestStep * 100.0);
		check (biggestStep <= 0.01,
		       "the gain never moves more than 1 % in one sample");

		// ...and it does get there. A smoother slow enough to pass the
		// check above and never arrive would be worse than a step.
		check (std::fabs (buffer[static_cast<size_t> (frames - 1) * 2]) < 1e-3,
		       "and it has arrived within 8192 samples (186 ms at 44.1 k)");

		// Both channels, together. A ramp applied to one channel only is
		// a moving image, and a mono test signal would never show it.
		bool channelsAgree = true;
		for (int i = 0; i < frames; ++i)
			channelsAgree &= (buffer[static_cast<size_t> (i) * 2]
			                  == buffer[static_cast<size_t> (i) * 2 + 1]);
		check (channelsAgree, "left and right are ramped by the same gain");

		// The ramp is a TIME, not a sample count. At twice the rate it
		// must take twice as many samples to cover the same ground, or a
		// 10 ms fade becomes 5 ms at 96 k.
		Project6Dsp fast;
		fast.setSampleRate (88200.0);
		fast.setOutputTrimDb (kTrimMaxDb);
		settle (fast);
		std::vector<float> quick (static_cast<size_t> (frames) * kChannelCount, 1.f);
		fast.setOutputTrimDb (kTrimMinDb);
		fast.applyOutputTrim (quick.data (), frames);

		double fastStep = 0.0;
		for (int i = 1; i < frames; ++i)
			fastStep = std::max (fastStep,
			                     std::fabs (static_cast<double> (quick[static_cast<size_t> (i) * 2])
			                                - quick[static_cast<size_t> (i - 1) * 2]));
		check (close (fastStep, biggestStep / 2.0, biggestStep * 0.05),
		       "doubling the sample rate halves the per-sample step");
	}

	//--------------------------------------------------------------------
	section ("4. The degenerate calls a real host makes");
	//--------------------------------------------------------------------
	{
		Project6Dsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setOutputTrimDb (-12.0);

		// A parameter-only block: numSamples == 0. Hosts send these and
		// the validator sends them deliberately.
		dsp.render (nullptr, 0);
		dsp.applyOutputTrim (nullptr, 0);
		dsp.render (nullptr, 64);
		dsp.applyOutputTrim (nullptr, 64);

		std::vector<float> buffer (128 * kChannelCount, 1.f);
		dsp.render (buffer.data (), 0);
		dsp.applyOutputTrim (buffer.data (), 0);
		check (buffer[0] == 1.f, "a zero-length block leaves the buffer alone");

		dsp.render (buffer.data (), -8);
		check (buffer[0] == 1.f, "so does a negative one");

		check (true, "null buffers and zero frames did not crash");

		// A rate change mid-life must not leave the smoother running at
		// the old rate's speed.
		dsp.setSampleRate (96000.0);
		check (close (dsp.sampleRate (), 96000.0, 0.0), "the rate change was taken");
		dsp.setSampleRate (0.0);
		check (close (dsp.sampleRate (), 96000.0, 0.0),
		       "a nonsense rate is ignored rather than dividing by zero");
	}

	//--------------------------------------------------------------------
	section ("5. THE DEFAULT PATCH'S OUTPUT LEVEL");
	//--------------------------------------------------------------------
	{
		// MEASURED BEFORE ANYONE PLAYS IT. A level that clips masks other
		// faults and sends you chasing the wrong bug, so the number is
		// printed whether or not it passes - and this is the measurement
		// the first real DSP has to repeat, unchanged, once there is
		// something to hear.
		const int frames = 1024;

		Project6Dsp dsp;
		dsp.setSampleRate (44100.0);
		dsp.setOutputTrimDb (kTrimDefaultDb);

		// (a) what the instrument actually emits today.
		std::vector<float> rendered (static_cast<size_t> (frames) * kChannelCount, 1.f);
		dsp.render (rendered.data (), frames);
		const double renderedPeak = peakOf (rendered);
		std::printf ("     default patch, rendered  : %s\n",
		             renderedPeak > 0.0
		                 ? (std::to_string (20.0 * std::log10 (renderedPeak)) + " dBFS").c_str ()
		                 : "silence (-inf dBFS)");
		check (renderedPeak == 0.0,
		       "the default patch is silent - there is nothing synthesised yet");

		// (b) what the output stage does to a full-scale signal at the
		// default trim. THIS is the number that must stay at 0.00 when
		// voices arrive: unity in, unity out.
		Project6Dsp stage;
		stage.setSampleRate (44100.0);
		stage.setOutputTrimDb (kTrimDefaultDb);
		std::vector<float> full (static_cast<size_t> (frames) * kChannelCount, 1.f);
		stage.applyOutputTrim (full.data (), frames);
		const double stagePeak = peakOf (full);
		std::printf ("     output stage, full scale : %.2f dBFS\n",
		             20.0 * std::log10 (stagePeak));
		check (close (stagePeak, 1.0, 1e-7),
		       "the default trim neither boosts nor attenuates");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
