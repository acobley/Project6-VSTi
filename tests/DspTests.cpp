//------------------------------------------------------------------------
// Project6 - DSP tests
//
// SDK-FREE, deliberately: Project6Dsp.{h,cpp} include no VST3 header, so
// this suite compiles and runs anywhere with
//
//     c++ -std=c++17 -O2 -Isource tests/DspTests.cpp
//         source/Project6Dsp.cpp source/Project6Sample.cpp
//         -o /tmp/dsptests && /tmp/dsptests
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
// Sections 1 to 5 are the output stage and the mappings around it - the
// stage every sample eventually passes through, and the one hardest to
// notice going wrong once there is audio to listen to. Section 6 is the
// sixty-four looping sample voices in front of it.
//
// The voice tests run the DSP at 1000 Hz. Not because anything does, but
// because the declick ramp is defined in SECONDS: at 1000 Hz it is five
// samples long instead of two hundred and twenty, so an assertion about
// what comes out after the fade-in can be written about sample 4 and read
// by a person. Every ratio under test is the same one it would be at
// 44.1 k.
//------------------------------------------------------------------------

#include "Project6Dsp.h"
#include "Project6Slots.h"

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
	section ("6. The sample voices");
	//--------------------------------------------------------------------
	{
		// Four frames, distinct in both channels and in both signs, so a
		// swapped channel, a reversed loop or an off-by-one playhead all
		// show up as a wrong number rather than as a wrong level.
		const float kLeft[4]  = {  0.10f,  0.20f,  0.30f,  0.40f };
		const float kRight[4] = { -0.15f, -0.25f, -0.35f, -0.45f };

		auto makeLoop = [&] (double rate)
		{
			SampleBuffer buffer;
			buffer.frameCount = 4;
			buffer.sourceRate = rate;
			buffer.samples.resize (4 * kSampleChannels);
			for (int f = 0; f < 4; ++f)
			{
				buffer.samples[static_cast<size_t> (f) * 2]     = kLeft[f];
				buffer.samples[static_cast<size_t> (f) * 2 + 1] = kRight[f];
			}
			buffer.peak = 0.45f;
			return buffer;
		};

		const SampleBuffer loop = makeLoop (1000.0);

		//----------------------------------------------------------------
		// Plays, in a loop, at the right values
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);          // declick = 5 samples
			dsp.setOutputTrimDb (kTrimMaxDb);    // unity, so the numbers are the file's
			dsp.setSlotSample (7, &loop);

			check (dsp.slotSample (7) == &loop, "a published sample is what the slot points at");
			check (! dsp.slotSounding (7),      "and publishing it does not start it");

			dsp.setSlotPlaying (7, true);
			check (dsp.slotSounding (7), "a rising edge starts the voice");

			const int frames = 16;
			std::vector<float> out (static_cast<size_t> (frames) * kChannelCount, 0.f);
			dsp.render (out.data (), frames);

			// The first five samples are the fade-in and are deliberately
			// NOT full level - that is checked below. From sample 4 the
			// envelope is at 1 and the output is the file, exactly.
			bool exact = true;
			for (int i = 4; i < frames; ++i)
			{
				exact &= (out[static_cast<size_t> (i) * 2]     == kLeft[i % 4]);
				exact &= (out[static_cast<size_t> (i) * 2 + 1] == kRight[i % 4]);
			}
			check (exact,
			       "once the fade-in is over the output IS the file, sample for sample");

			// Which means it looped: samples 4..15 are three times round a
			// four-frame file.
			check (out[static_cast<size_t> (8) * 2] == kLeft[0],
			       "and frame 8 is the start of the loop again");

			// NEGATIVE CONTROL for that comparison. If the voice had not
			// run at all, every sample would be zero and the loop above
			// would have compared nothing - so assert it is NOT silent.
			double peak = 0.0;
			for (float v : out)
				peak = std::max (peak, static_cast<double> (std::fabs (v)));
			check (peak > 0.4, "NEGATIVE CONTROL: the voice really did produce audio");

			// The channels are not the same signal.
			check (out[8] != out[9], "left and right are the file's own two channels");
		}

		//----------------------------------------------------------------
		// The fade-in, and the fade-out that follows a second click
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (0, &loop);
			dsp.setSlotPlaying (0, true);

			std::vector<float> out (64 * kChannelCount, 0.f);
			dsp.render (out.data (), 64);

			// A LOOP DOES NOT START AT ZERO: the first sample of this file
			// is 0.1, and cutting it in at full gain would be a step.
			check (std::fabs (out[0]) < std::fabs (kLeft[0]),
			       "the first sample is attenuated by the fade-in");
			check (out[0] != 0.f, "but the fade starts moving immediately");
			check (std::fabs (out[0]) < std::fabs (out[2]),
			       "and the envelope rises");

			// Click again: it fades out and then stops on its own.
			dsp.setSlotPlaying (0, false);
			check (dsp.slotSounding (0), "the falling edge does not stop it dead");

			std::vector<float> tail (64 * kChannelCount, 0.f);
			dsp.render (tail.data (), 64);
			check (! dsp.slotSounding (0), "it stops once the fade-out reaches zero");

			// Five samples of fade, then nothing at all.
			bool silentAfterFade = true;
			for (int i = 8; i < 64; ++i)
				silentAfterFade &= (tail[static_cast<size_t> (i) * 2] == 0.f
				                    && tail[static_cast<size_t> (i) * 2 + 1] == 0.f);
			check (silentAfterFade, "and produces nothing afterwards");

			// And it starts again FROM THE BEGINNING.
			dsp.setSlotPlaying (0, true);
			std::vector<float> again (16 * kChannelCount, 0.f);
			dsp.render (again.data (), 16);
			check (again[static_cast<size_t> (4) * 2] == kLeft[0],
			       "a fresh click restarts the loop from frame 0");
		}

		//----------------------------------------------------------------
		// A re-click during the fade-out is a change of mind
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (1, &loop);
			dsp.setSlotPlaying (1, true);

			std::vector<float> settle (64 * kChannelCount, 0.f);
			dsp.render (settle.data (), 64);
			const double before = dsp.slotPosition (1);

			// Two samples into the fade, change our mind.
			dsp.setSlotPlaying (1, false);
			std::vector<float> partial (2 * kChannelCount, 0.f);
			dsp.render (partial.data (), 2);
			dsp.setSlotPlaying (1, true);

			check (dsp.slotSounding (1), "the voice survived the reversal");
			// The playhead CARRIED ON. Jumping it back to zero here would
			// be exactly the click the fade exists to prevent.
			check (dsp.slotPosition (1) != before,
			       "and the playhead kept moving rather than jumping back");

			std::vector<float> after (16 * kChannelCount, 0.f);
			dsp.render (after.data (), 16);
			check (after[static_cast<size_t> (15) * 2] != 0.f,
			       "and it is back at full level");
		}

		//----------------------------------------------------------------
		// The file's own rate, against the session's
		//----------------------------------------------------------------
		{
			const SampleBuffer fast = makeLoop (2000.0);   // twice the session rate

			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (2, &fast);
			dsp.setSlotPlaying (2, true);

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);

			// Two source frames per output frame: 0, 2, 0, 2 ... A voice
			// that ignored sourceRate would play this file a fifth flat.
			bool stepped = true;
			for (int i = 4; i < 16; ++i)
				stepped &= (out[static_cast<size_t> (i) * 2] == kLeft[(i * 2) % 4]);
			check (stepped, "a file at twice the session rate advances two frames a sample");

			// NEGATIVE CONTROL: that is NOT what a one-to-one voice does.
			check (out[static_cast<size_t> (5) * 2] != kLeft[5 % 4],
			       "NEGATIVE CONTROL: which is not the same as ignoring the rate");
		}

		//----------------------------------------------------------------
		// Slots with nothing in them, and slots emptied underneath
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);

			// Clicking an empty slot must do nothing at all - not crash,
			// and not leave a voice running that produces silence for ever
			// and holds the host's silence flag down.
			dsp.setSlotPlaying (5, true);
			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);

			double peak = 0.0;
			for (float v : out)
				peak = std::max (peak, static_cast<double> (std::fabs (v)));
			check (peak == 0.0, "an empty slot produces silence");
			check (! dsp.slotSounding (5), "and does not leave a voice running");

			// Emptied while playing - which is what dropping a new file
			// on a sounding slot does for an instant.
			dsp.setSlotSample (6, &loop);
			dsp.setSlotPlaying (6, true);
			dsp.render (out.data (), 16);
			check (dsp.slotSounding (6), "a loaded slot is sounding");

			dsp.setSlotSample (6, nullptr);
			dsp.render (out.data (), 16);
			check (! dsp.slotSounding (6), "and stops when its sample is taken away");

			// Out-of-range indices are refused rather than clamped, the
			// same rule the slot bank follows.
			dsp.setSlotPlaying (-1, true);
			dsp.setSlotPlaying (kSlotCount, true);
			dsp.setSlotSample (kSlotCount, &loop);
			check (dsp.soundingVoiceCount () == 0, "a bad slot index starts nothing");
			check (dsp.slotSample (kSlotCount) == nullptr, "and publishes nothing");
		}

		//----------------------------------------------------------------
		// Several at once, and what the host is told about silence
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);

			const SampleBuffer other = makeLoop (1000.0);
			dsp.setSlotSample (10, &loop);
			dsp.setSlotSample (63, &other);
			dsp.setSlotPlaying (10, true);
			dsp.setSlotPlaying (63, true);

			check (dsp.soundingVoiceCount () == 2, "two voices are sounding");

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);

			// They SUM. Two copies of the same file is twice the file -
			// not one of them, and not an average.
			check (close (out[static_cast<size_t> (8) * 2], 2.0 * kLeft[0], 1e-6),
			       "and they sum rather than replacing each other");

			// reset() is what setActive(false) calls: everything stops
			// dead, because there is no block left to fade in.
			dsp.reset ();
			check (dsp.soundingVoiceCount () == 0, "reset stops every voice dead");
			check (dsp.slotSample (10) == &loop,
			       "but leaves the samples loaded - they are still in their slots");
		}

		//----------------------------------------------------------------
		// A slot's own level
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (20, &loop);

			// THE DEFAULT IS UNITY, and exactly so - the multiply has to
			// be by 1.0 or a pad at its default level stops being
			// bit-identical to its file.
			check (close (dbToLinear (kSlotLevelDefaultDb, kSlotLevelMinDb), 1.0, 0.0),
			       "a slot's default level is exactly unity gain");

			dsp.setSlotLevelDb (20, kSlotLevelDefaultDb);
			dsp.setSlotPlaying (20, true);

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);
			check (out[static_cast<size_t> (8) * 2] == kLeft[0],
			       "so the file comes through the level stage unchanged");

			// The bottom of the travel is OFF, not -40 dB of leakage.
			check (dbToLinear (kSlotLevelMinDb, kSlotLevelMinDb) == 0.0,
			       "the bottom of a slot's travel is exactly zero");

			// A COMPONENT LEVEL, so it may lift as well as cut. The stage
			// that must not boost is the output trim.
			check (kSlotLevelMaxDb > 0.0, "a slot's level can boost");
			check (kTrimMaxDb == 0.0, "and the output trim still cannot");
		}

		{
			// SNAPPED ON START. A pad set quiet must come in quiet, not
			// ramp down to it over the first ten milliseconds.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (21, &loop);
			dsp.setSlotLevelDb (21, -20.0);           // one tenth
			dsp.setSlotPlaying (21, true);

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);

			// Sample 4 is the first at full declick envelope.
			check (close (out[static_cast<size_t> (4) * 2], kLeft[0] * 0.1, 1e-6),
			       "a voice starts AT its level rather than ramping to it");

			// NEGATIVE CONTROL: which is not the same as playing at unity
			// and getting quieter afterwards.
			check (! close (out[static_cast<size_t> (4) * 2], kLeft[0], 1e-6),
			       "NEGATIVE CONTROL: and not at full level for the first samples");
		}

		{
			// SMOOTHED WHILE RUNNING. A bar dragged during playback moves
			// every block, and a block-rate step on a gain is a click on
			// every buffer boundary.
			//
			// AT 44.1 k, not the 1000 Hz the rest of this section uses:
			// the smoother is defined in seconds, so the "no more than a
			// per cent a sample" bound only means anything at a real rate.
			// At 1000 Hz the same ten milliseconds is ten samples, and
			// moving a tenth of the way per sample is correct there.
			const SampleBuffer real = makeLoop (44100.0);

			Project6Dsp dsp;
			dsp.setSampleRate (44100.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (22, &real);
			dsp.setSlotLevelDb (22, kSlotLevelDefaultDb);
			dsp.setSlotPlaying (22, true);

			std::vector<float> settle (4096 * kChannelCount, 0.f);
			dsp.render (settle.data (), 4096);
			check (close (dsp.slotLevelGain (22), 1.0, 1e-9), "settled at unity");

			// Ask for silence, hard, and watch it walk there one sample at
			// a time.
			dsp.setSlotLevelDb (22, kSlotLevelMinDb);

			double biggestStep = 0.0;
			double previous = dsp.slotLevelGain (22);
			std::vector<float> one (1 * kChannelCount, 0.f);
			// 5000 samples is 113 ms - eleven time constants, so a
			// smoother that is genuinely arriving has arrived.
			for (int i = 0; i < 5000; ++i)
			{
				dsp.render (one.data (), 1);
				const double now = dsp.slotLevelGain (22);
				biggestStep = std::max (biggestStep, std::fabs (now - previous));
				previous = now;
			}

			std::printf ("     largest single-sample level step: %.5f (%.3f %%)\n",
			             biggestStep, biggestStep * 100.0);
			check (biggestStep <= 0.01,
			       "the level never moves more than 1 % in one sample");
			check (biggestStep > 0.0, "NEGATIVE CONTROL: and it does move");
			check (previous < 1e-3, "and it does arrive");

			// The voice is still SOUNDING - a level of zero is not a stop,
			// and the pad stays lit because the user did not un-arm it.
			check (dsp.slotSounding (22),
			       "a level of zero silences a pad without stopping it");
		}

		{
			// Out-of-range indices, and a level outside its own travel.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setSlotLevelDb (-1, 0.0);
			dsp.setSlotLevelDb (kSlotCount, 0.0);
			check (dsp.slotLevelGain (kSlotCount) == 0.0, "a bad slot index has no level");

			dsp.setSlotLevelDb (0, 1000.0);
			check (close (dsp.slotLevelGain (0),
			              dbToLinear (kSlotLevelMaxDb, kSlotLevelMinDb), 1e-9),
			       "a level above the travel is clamped to the top of it");
			dsp.setSlotLevelDb (0, -1000.0);
			check (dsp.slotLevelGain (0) == 0.0, "and one below it is silence");
		}

		//----------------------------------------------------------------
		// The row buses
		//----------------------------------------------------------------
		{
			// Two pads on DIFFERENT rows, so a row level that leaked into
			// the wrong bus shows up as a wrong number rather than as a
			// wrong level everywhere.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);

			const int onRowZero = slotIndex (0, 0);
			const int onRowOne  = slotIndex (0, 1);
			check (rowOfSlot (onRowZero) == 0 && rowOfSlot (onRowOne) == 1,
			       "the two test pads really are on different rows");

			dsp.setSlotSample (onRowZero, &loop);
			dsp.setSlotSample (onRowOne, &loop);
			dsp.setSlotPlaying (onRowZero, true);
			dsp.setSlotPlaying (onRowOne, true);

			// Both rows at unity: the two pads simply sum.
			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);
			check (close (out[static_cast<size_t> (8) * 2], 2.0 * kLeft[0], 1e-6),
			       "at unity the rows just sum, as before");

			// Now pull row 0 down. Row 1 must not move.
			dsp.setRowLevelDb (0, -20.0);                 // one tenth
			std::vector<float> settled (4096 * kChannelCount, 0.f);
			dsp.render (settled.data (), 4096);

			check (close (dsp.rowLevelGain (0), 0.1, 1e-6), "row 0 arrived at its level");
			check (close (dsp.rowLevelGain (1), 1.0, 1e-9), "and row 1 did not move");

			std::vector<float> after (16 * kChannelCount, 0.f);
			dsp.render (after.data (), 16);

			// 0.1 of one pad plus 1.0 of the other.
			const double expected = kLeft[0] * 0.1 + kLeft[0] * 1.0;
			check (close (after[static_cast<size_t> (8) * 2], expected, 1e-5),
			       "a row's level scales that row and leaves the others alone");

			// NEGATIVE CONTROL: which is not the same as scaling everything.
			check (! close (after[static_cast<size_t> (8) * 2], 2.0 * kLeft[0] * 0.1, 1e-5),
			       "NEGATIVE CONTROL: it is not a master gain in disguise");
		}

		{
			// TWO PADS ON ONE ROW go through ONE row level - the level is
			// on the SUM, which is the whole point of a bus.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);

			dsp.setSlotSample (slotIndex (2, 3), &loop);
			dsp.setSlotSample (slotIndex (5, 3), &loop);
			dsp.setSlotLevelDb (slotIndex (2, 3), kSlotLevelDefaultDb);
			dsp.setSlotLevelDb (slotIndex (5, 3), kSlotLevelDefaultDb);
			dsp.setRowLevelDb (3, -20.0);           // set BEFORE anything starts

			dsp.setSlotPlaying (slotIndex (2, 3), true);
			dsp.setSlotPlaying (slotIndex (5, 3), true);

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);

			// SNAPPED, not ramped: the row was silent when the fader was
			// moved, so it was already in place when the pads started.
			check (close (out[static_cast<size_t> (8) * 2], 2.0 * kLeft[0] * 0.1, 1e-5),
			       "a row level set while the row is silent is in place at the start");
			check (close (dsp.rowLevelGain (3), 0.1, 1e-9),
			       "the row gain snapped rather than ramping");
		}

		{
			// The ends of the travel, and a bad row.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);

			check (close (dbToLinear (kRowLevelDefaultDb, kRowLevelMinDb), 1.0, 0.0),
			       "a row's default level is exactly unity");
			check (dbToLinear (kRowLevelMinDb, kRowLevelMinDb) == 0.0,
			       "and the bottom of its travel is exactly zero");

			dsp.setRowLevelDb (0, 1000.0);
			check (close (dsp.rowLevelGain (0),
			              dbToLinear (kRowLevelMaxDb, kRowLevelMinDb), 1e-9),
			       "a row level above the travel is clamped to the top of it");

			dsp.setRowLevelDb (-1, 0.0);
			dsp.setRowLevelDb (kSlotRows, 0.0);
			check (dsp.rowLevelGain (kSlotRows) == 0.0, "a bad row index has no level");
		}

		{
			// THE CHUNKING FALLBACK. renderVoices splits a block that is
			// bigger than the row scratch, because growing the scratch
			// would be an allocation on the audio thread. A host that
			// told us its maximum never takes this path - so it is worth
			// proving it produces the same audio when it does.
			const SampleBuffer real = makeLoop (1000.0);
			const int frames = 300;

			Project6Dsp whole;
			whole.setSampleRate (1000.0);
			whole.setOutputTrimDb (kTrimMaxDb);
			whole.setMaxBlockSize (frames);
			whole.setSlotSample (0, &real);
			whole.setSlotSample (40, &real);
			whole.setRowLevelDb (5, -6.0);
			whole.setSlotPlaying (0, true);
			whole.setSlotPlaying (40, true);

			Project6Dsp chunked;
			chunked.setSampleRate (1000.0);
			chunked.setOutputTrimDb (kTrimMaxDb);
			chunked.setMaxBlockSize (7);            // absurdly small on purpose
			chunked.setSlotSample (0, &real);
			chunked.setSlotSample (40, &real);
			chunked.setRowLevelDb (5, -6.0);
			chunked.setSlotPlaying (0, true);
			chunked.setSlotPlaying (40, true);

			std::vector<float> a (static_cast<size_t> (frames) * kChannelCount, 0.f);
			std::vector<float> b (static_cast<size_t> (frames) * kChannelCount, 0.f);
			whole.render (a.data (), frames);
			chunked.render (b.data (), frames);

			check (a == b, "a scratch far too small gives bit-identical audio");

			// NEGATIVE CONTROL for that comparison: it must be capable of
			// failing, so compare against a run that really is different.
			Project6Dsp other;
			other.setSampleRate (1000.0);
			other.setOutputTrimDb (kTrimMaxDb);
	other.setSlotSample (0, &real);
			other.setSlotPlaying (0, true);
			std::vector<float> c (static_cast<size_t> (frames) * kChannelCount, 0.f);
			other.render (c.data (), frames);
			check (! (a == c), "NEGATIVE CONTROL: and the comparison can fail");
		}

		//----------------------------------------------------------------
		// The progress the panel draws
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (9, &loop);

			check (dsp.slotProgress (9) == 0.f, "an idle slot is at zero");

			dsp.setSlotPlaying (9, true);
			check (dsp.slotProgress (9) == 0.f, "and so is one that has not rendered yet");

			// The loop is four frames at the session rate, so after two
			// frames the playhead is half way through it.
			std::vector<float> out (2 * kChannelCount, 0.f);
			dsp.render (out.data (), 2);
			check (close (dsp.slotProgress (9), 0.5, 1e-6),
			       "two frames into a four-frame file is half way");

			dsp.render (out.data (), 1);
			check (close (dsp.slotProgress (9), 0.75, 1e-6), "three frames is three quarters");

			// IT WRAPS WITH THE LOOP, because it is the playhead and the
			// playhead wraps. A bar that ran off the end would be a bar
			// measuring something other than what is heard.
			dsp.render (out.data (), 1);
			check (close (dsp.slotProgress (9), 0.0, 1e-6),
			       "and the fourth wraps back to the start");

			bool inRange = true;
			for (int i = 0; i < 40; ++i)
			{
				dsp.render (out.data (), 1);
				const float p = dsp.slotProgress (9);
				inRange &= (p >= 0.f && p < 1.f);
			}
			check (inRange, "and it stays inside 0..1 for ever");

			// STOPPING CLEARS IT, so a bar never lingers on a pad that
			// has gone quiet - which would be the panel claiming
			// something the audio is not doing.
			dsp.setSlotPlaying (9, false);
			std::vector<float> tail (64 * kChannelCount, 0.f);
			dsp.render (tail.data (), 64);
			check (! dsp.slotSounding (9), "the voice has stopped");
			check (dsp.slotProgress (9) == 0.f, "and its progress is back to zero");

			// NEGATIVE CONTROL: it is not simply always zero.
			dsp.setSlotPlaying (9, true);
			dsp.render (out.data (), 2);
			check (dsp.slotProgress (9) > 0.f,
			       "NEGATIVE CONTROL: and it is not stuck at zero");

			// A sample taken away underneath a running voice clears it too.
			dsp.setSlotSample (9, nullptr);
			dsp.render (out.data (), 2);
			check (dsp.slotProgress (9) == 0.f,
			       "a slot emptied while playing clears its progress");

			check (dsp.slotProgress (-1) == 0.f, "a bad slot index has no progress");
			check (dsp.slotProgress (kSlotCount) == 0.f, "at either end");

			// reset() is setActive(false): everything stops dead, and no
			// bar should survive it.
			Project6Dsp other;
			other.setSampleRate (1000.0);
			other.setSlotSample (11, &loop);
			other.setSlotPlaying (11, true);
			other.render (out.data (), 2);
			check (other.slotProgress (11) > 0.f, "a running pad has progress");
			other.reset ();
			check (other.slotProgress (11) == 0.f, "and reset clears it");
		}

		//----------------------------------------------------------------
		// The row direct outs, tapped BEFORE the row fader
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);

			const int onRowTwo = slotIndex (3, 2);
			dsp.setSlotSample (onRowTwo, &loop);
			dsp.setSlotPlaying (onRowTwo, true);

			const int frames = 16;
			std::vector<std::vector<float>> taps (
				kSlotRows, std::vector<float> (
					static_cast<size_t> (frames) * kChannelCount, 0.f));
			float* rowOuts[kSlotRows];
			for (int row = 0; row < kSlotRows; ++row)
				rowOuts[row] = taps[static_cast<size_t> (row)].data ();

			std::vector<float> out (static_cast<size_t> (frames) * kChannelCount, 0.f);
			dsp.render (out.data (), rowOuts, frames);

			check (close (taps[2][static_cast<size_t> (8) * 2], kLeft[0], 1e-6),
			       "a row's direct out carries that row's pads");

			// EVERY OTHER ROW IS SILENT. A tap that carried the mix would
			// be a mix, not a direct out.
			bool othersSilent = true;
			for (int row = 0; row < kSlotRows; ++row)
			{
				if (row == 2)
					continue;
				for (float v : taps[static_cast<size_t> (row)])
					othersSilent &= (v == 0.f);
			}
			check (othersSilent, "and no other row's out carries anything");

			// THE TAP IS PRE-FADER. Pull the row down to a tenth: the mix
			// follows, the direct out does not move at all.
			dsp.setRowLevelDb (2, -20.0);
			std::vector<float> settle (4096 * kChannelCount, 0.f);
			dsp.render (settle.data (), frames > 0 ? 4096 : 0);

			std::vector<float> mixed (static_cast<size_t> (frames) * kChannelCount, 0.f);
			for (auto& tap : taps)
				std::fill (tap.begin (), tap.end (), 0.f);
			dsp.render (mixed.data (), rowOuts, frames);

			check (close (mixed[static_cast<size_t> (8) * 2], kLeft[0] * 0.1, 1e-5),
			       "the row fader scales what reaches the mix");
			check (close (taps[2][static_cast<size_t> (8) * 2], kLeft[0], 1e-6),
			       "and does NOT touch the direct out - the tap is pre-fader");

			// NEGATIVE CONTROL: if the tap were post-fader the two would
			// now agree, and that comparison has to be able to fail.
			check (! close (taps[2][static_cast<size_t> (8) * 2],
			                mixed[static_cast<size_t> (8) * 2], 1e-5),
			       "NEGATIVE CONTROL: the two are genuinely different signals");

			// And the OUTPUT TRIM is not on the direct outs either: they
			// left before the row fader, let alone the master.
			dsp.setOutputTrimDb (-20.0);
			dsp.render (settle.data (), 4096);
			for (auto& tap : taps)
				std::fill (tap.begin (), tap.end (), 0.f);
			dsp.render (mixed.data (), rowOuts, frames);
			check (close (taps[2][static_cast<size_t> (8) * 2], kLeft[0], 1e-6),
			       "and neither does the output trim");
		}

		{
			// A SILENT ROW SENDS SILENCE, and does not send whatever the
			// host left in the buffer last block. The row is skipped
			// entirely when nothing on it sounds, so render() has to
			// clear the taps itself.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);

			const int frames = 8;
			std::vector<std::vector<float>> taps (
				kSlotRows, std::vector<float> (
					static_cast<size_t> (frames) * kChannelCount, 0.9f));   // dirty
			float* rowOuts[kSlotRows];
			for (int row = 0; row < kSlotRows; ++row)
				rowOuts[row] = taps[static_cast<size_t> (row)].data ();

			std::vector<float> out (static_cast<size_t> (frames) * kChannelCount, 0.f);
			dsp.render (out.data (), rowOuts, frames);

			bool clean = true;
			for (const auto& tap : taps)
				for (float v : tap)
					clean &= (v == 0.f);
			check (clean, "a dirty buffer handed to a silent row comes back silent");

			check (! dsp.rowSounding (0), "and the row reports itself silent");

			dsp.setSlotSample (slotIndex (0, 4), &loop);
			dsp.setSlotPlaying (slotIndex (0, 4), true);
			check (dsp.rowSounding (4), "a row with a pad on it reports itself sounding");
			check (! dsp.rowSounding (3), "and its neighbours do not");
			check (! dsp.rowSounding (-1) && ! dsp.rowSounding (kSlotRows),
			       "a bad row index is not sounding");
		}

		{
			// NULL IS LEGAL, everywhere. A host may deactivate a bus, and
			// the single-buffer render() passes no array at all.
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setSlotSample (0, &loop);
			dsp.setSlotPlaying (0, true);

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), nullptr, 16);

			float* someNull[kSlotRows] = { nullptr };
			std::vector<float> only (16 * kChannelCount, 0.f);
			someNull[3] = only.data ();          // one bus active, seven not
			dsp.render (out.data (), someNull, 16);

			check (true, "null row buffers, and a mix of null and not, do not crash");
		}

		//----------------------------------------------------------------
		// The trim is still the last stage
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (-6.020599913279624);      // half
			dsp.setSlotSample (4, &loop);
			dsp.setSlotPlaying (4, true);

			std::vector<float> out (32 * kChannelCount, 0.f);
			dsp.render (out.data (), 32);

			check (close (out[static_cast<size_t> (16) * 2], kLeft[0] * 0.5, 1e-6),
			       "the output trim scales the voices, not the other way round");
		}
	}

	//--------------------------------------------------------------------
	section ("7. Loop, or play once and stop");
	//--------------------------------------------------------------------
	{
		// A four-frame file at the session rate, so one pass is four
		// samples and the arithmetic is done in the head.
		SampleBuffer four;
		four.frameCount = 4;
		four.sourceRate = 1000.0;
		four.samples.assign (4 * kSampleChannels, 0.f);
		for (int f = 0; f < 4; ++f)
		{
			four.samples[static_cast<size_t> (f) * 2]     = 0.25f * (f + 1);
			four.samples[static_cast<size_t> (f) * 2 + 1] = 0.25f * (f + 1);
		}

		//----------------------------------------------------------------
		// THE DEFAULT IS LOOP, which is what this instrument is for and
		// what every pad did before there was a choice.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			check (dsp.slotLoop (0), "a pad loops unless it is told not to");
			check (dsp.slotLoop (-1), "and a bad index answers the default");

			dsp.setSlotLoop (0, false);
			check (! dsp.slotLoop (0), "and the setting is remembered");
			dsp.setSlotLoop (0, true);
			check (dsp.slotLoop (0), "both ways");
		}

		//----------------------------------------------------------------
		// A LOOPING PAD GOES ROUND. Sixteen samples of a four-frame file
		// is four passes, and it is still sounding at the end of them.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (3, &four);
			dsp.setSlotPlaying (3, true);

			std::vector<float> out (64 * kChannelCount, 0.f);
			dsp.render (out.data (), 64);
			check (dsp.slotSounding (3), "a looping pad is still going after sixteen passes");
		}

		//----------------------------------------------------------------
		// A ONE-SHOT STOPS. The declick fade is five milliseconds - five
		// samples at this rate - so it is silent a few samples after the
		// end of the file and not on the very sample.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (3, &four);
			dsp.setSlotLoop (3, false);
			dsp.setSlotPlaying (3, true);

			std::vector<float> out (64 * kChannelCount, 0.f);
			dsp.render (out.data (), 3);
			check (dsp.slotSounding (3), "it is still sounding part way through its file");

			dsp.render (out.data (), 61);
			check (! dsp.slotSounding (3), "and has stopped itself by the end of the block");

			// NEGATIVE CONTROL for the whole feature: the identical pad
			// left looping is still going.
			Project6Dsp looping;
			looping.setSampleRate (1000.0);
			looping.setSlotSample (3, &four);
			looping.setSlotPlaying (3, true);
			looping.render (out.data (), 64);
			check (looping.slotSounding (3),
			       "NEGATIVE CONTROL: the same pad set to loop is not stopped");
		}

		//----------------------------------------------------------------
		// IT FADES RATHER THAN CUTS. The last sample of a file is no more
		// likely to be at zero than the first, and a one-shot that
		// clicked when it finished would be worse than one that looped.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (3, &four);
			dsp.setSlotLoop (3, false);
			dsp.setSlotPlaying (3, true);

			std::vector<float> out (16 * kChannelCount, 0.f);
			dsp.render (out.data (), 16);

			// Somewhere after the file ends there is audio that is
			// neither the file nor silence: that is the fade.
			bool faded = false;
			for (int i = 4; i < 16; ++i)
			{
				const float v = out[static_cast<size_t> (i) * 2];
				faded |= (v != 0.f);
			}
			check (faded, "the tail after the file is a fade, not a cut");

			// And it does reach silence. A fade that never finished would
			// leave a voice in the mix for ever.
			check (out[static_cast<size_t> (15) * 2] == 0.f, "which reaches zero");
		}

		//----------------------------------------------------------------
		// SWITCHED WHILE RUNNING, it takes effect at the next end of the
		// file rather than cutting the pass it is on.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setSlotSample (3, &four);
			dsp.setSlotPlaying (3, true);

			std::vector<float> out (2 * kChannelCount, 0.f);
			dsp.render (out.data (), 2);          // two frames into the file

			dsp.setSlotLoop (3, false);
			check (dsp.slotSounding (3), "switching to one-shot mid-pass does not cut it");

			dsp.render (out.data (), 2);          // ...to the end of the file
			check (dsp.slotSounding (3), "it is still finishing the pass it was on");

			std::vector<float> tail (64 * kChannelCount, 0.f);
			dsp.render (tail.data (), 64);
			check (! dsp.slotSounding (3), "and stops at the end of it");
		}
	}

	//--------------------------------------------------------------------
	section ("8. Fitting a pad's file to the project's tempo");
	//--------------------------------------------------------------------
	{
		// A file that KNOWS its own tempo. Sixteen frames at 1000 Hz is
		// 16 ms of audio; the numbers below only care about the ratio.
		auto makeTimed = [] (double tempoBpm, bool oneShot)
		{
			SampleBuffer buffer;
			buffer.frameCount = 16;
			buffer.sourceRate = 1000.0;
			buffer.samples.assign (16 * kSampleChannels, 0.f);
			for (int f = 0; f < 16; ++f)
				buffer.samples[static_cast<size_t> (f) * 2] = 0.1f * (f + 1);
			buffer.tempoBpm = tempoBpm;
			buffer.oneShot  = oneShot;
			buffer.tempoSource = TempoSource::AcidTempo;
			return buffer;
		};

		const SampleBuffer at100 = makeTimed (100.0, false);
		const SampleBuffer hit   = makeTimed (100.0, true);
		const SampleBuffer noTempo = makeTimed (0.0, false);

		//----------------------------------------------------------------
		// The speed the pad ends up at, which is what everything else
		// hangs off.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setSlotSample (5, &at100);

			// No project tempo yet: NOTHING is fitted, whatever the mode.
			dsp.setSlotFitMode (5, FitMode::Varispeed);
			check (close (dsp.slotFitSpeed (5), 1.0, 1e-12),
			       "with no project tempo nothing is fitted");

			// The example from the request: a 100 BPM file, a 90 BPM
			// project, and the file plays at 0.9.
			dsp.setProjectTempo (90.0);
			check (close (dsp.projectTempo (), 90.0, 1e-12), "the project tempo is kept");
			check (close (dsp.slotFitSpeed (5), 0.9, 1e-12),
			       "a 100 BPM file in a 90 BPM project plays at 0.9x");

			// OFF means off, and it is per pad.
			dsp.setSlotFitMode (5, FitMode::Off);
			check (dsp.slotFitMode (5) == FitMode::Off, "the mode is remembered");
			check (close (dsp.slotFitSpeed (5), 1.0, 1e-12), "and Off is not fitted");

			dsp.setSlotFitMode (5, FitMode::PitchPreserved);
			check (close (dsp.slotFitSpeed (5), 0.9, 1e-12),
			       "the pitch-preserving mode asks for the same speed");

			// A ONE-SHOT is never fitted. This is the rule that keeps a
			// snare from being stretched by a bar of arithmetic.
			dsp.setSlotSample (5, &hit);
			check (close (dsp.slotFitSpeed (5), 1.0, 1e-12), "a one-shot is never fitted");
			check (! close (dsp.slotFitSpeed (5), 0.9, 1e-12),
			       "NEGATIVE CONTROL: and the same file without the flag would have been");

			// Neither is a file whose tempo could not be read.
			dsp.setSlotSample (5, &noTempo);
			check (close (dsp.slotFitSpeed (5), 1.0, 1e-12),
			       "a file with no detected tempo plays as it arrived");

			// An empty slot and a bad index answer 1.0 rather than
			// dividing by something that is not there.
			dsp.setSlotSample (5, nullptr);
			check (close (dsp.slotFitSpeed (5), 1.0, 1e-12), "an empty slot is 1.0");
			check (close (dsp.slotFitSpeed (-1), 1.0, 1e-12), "and so is a bad index");
			check (dsp.slotFitMode (kSlotCount) == FitMode::Off, "at either end");
		}

		//----------------------------------------------------------------
		// And that speed actually moves the playhead. A speed reported
		// but not applied is the failure this whole project keeps coming
		// back to - a control that looks live and is not.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);          // so the step is exactly 1
			dsp.setOutputTrimDb (kTrimMaxDb);
			dsp.setSlotSample (6, &at100);
			dsp.setProjectTempo (90.0);
			dsp.setSlotFitMode (6, FitMode::Varispeed);
			dsp.setSlotPlaying (6, true);

			std::vector<float> out (8 * kChannelCount, 0.f);
			dsp.render (out.data (), 8);

			// Eight output samples at 0.9x is 7.2 source frames.
			check (close (dsp.slotPosition (6), 7.2, 1e-9),
			       "eight samples at 0.9x is 7.2 frames in");
			check (! close (dsp.slotPosition (6), 8.0, 1e-9),
			       "NEGATIVE CONTROL: it is not still playing at 1.0x");

			// The PROGRESS the panel draws is that same musical position.
			check (close (dsp.slotProgress (6), 7.2 / 16.0, 1e-6),
			       "and the progress bar reads the same position");

			// A tempo change mid-loop is picked up on the next block,
			// because the speed is computed per block and never cached.
			// Three samples only, so the answer does not wrap round the
			// sixteen-frame file and land back where it started - which
			// would be a test that passed whether or not it worked.
			dsp.setProjectTempo (200.0);
			dsp.render (out.data (), 3);
			check (close (dsp.slotPosition (6), 7.2 + 3 * 2.0, 1e-9),
			       "a tempo change takes effect on the next block");
			check (! close (dsp.slotPosition (6), 7.2 + 3 * 0.9, 1e-9),
			       "NEGATIVE CONTROL: the old speed is not cached anywhere");
		}

		//----------------------------------------------------------------
		// Switching modes must not move the music.
		//----------------------------------------------------------------
		{
			Project6Dsp dsp;
			dsp.setSampleRate (1000.0);
			dsp.setSlotSample (2, &at100);
			dsp.setProjectTempo (90.0);
			dsp.setSlotFitMode (2, FitMode::Varispeed);
			dsp.setSlotPlaying (2, true);

			std::vector<float> out (4 * kChannelCount, 0.f);
			dsp.render (out.data (), 4);
			const double before = dsp.slotPosition (2);

			dsp.setSlotFitMode (2, FitMode::PitchPreserved);
			check (close (dsp.slotPosition (2), before, 1e-12),
			       "changing mode does not move the playhead");
			check (before > 0.0, "NEGATIVE CONTROL: and it had moved in the first place");
		}
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
