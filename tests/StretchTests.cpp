//------------------------------------------------------------------------
// Project6 - tempo fit tests
//
// SDK-FREE. The point of these is that they MEASURE THE PITCH rather than
// trust the mode's name: a sine goes in, and the test counts the zero
// crossings that come out. That contrast is the whole negative control of
// this file - the same sine through varispeed comes out at a different
// frequency, and through the stretcher at the same one, and if either of
// those two facts stopped being true the feature would be silently doing
// nothing while still reporting a speed.
//
//     c++ -std=c++17 -O2 -Wall -Isource tests/StretchTests.cpp
//         source/Project6Stretch.cpp -o ~/p6obj/stretchtests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//------------------------------------------------------------------------

#include "Project6Stretch.h"
#include "Project6Sample.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

constexpr double kRate = 44100.0;

/** A whole number of cycles of a sine, so the file loops seamlessly and
    any discontinuity the test measures came from the stretcher and not
    from the loop point. 441 Hz at 44100 is a period of exactly 100
    samples. */
std::vector<float> sineLoop (double frequency, int frames)
{
	std::vector<float> data (static_cast<std::size_t> (frames) * kSampleChannels, 0.f);
	for (int i = 0; i < frames; ++i)
	{
		const double value = std::sin (2.0 * 3.14159265358979323846 * frequency * i / kRate);
		data[static_cast<std::size_t> (i) * kSampleChannels]     = static_cast<float> (value);
		data[static_cast<std::size_t> (i) * kSampleChannels + 1] = static_cast<float> (value);
	}
	return data;
}

struct Rendered
{
	std::vector<double> left;
	double endPosition = 0.0;
};

/** Run the stretcher for `outputFrames` output samples. */
Rendered render (const std::vector<float>& source, int frames, double speed, FitMode mode,
                 int outputFrames, double rateStep = 1.0)
{
	TimeStretcher stretcher;
	stretcher.setSampleRate (kRate);

	Rendered result;
	result.left.reserve (static_cast<std::size_t> (outputFrames));

	for (int i = 0; i < outputFrames; ++i)
	{
		double l = 0.0, r = 0.0;
		stretcher.next (source.data (), frames, rateStep, speed, mode, l, r);
		result.left.push_back (l);
	}
	result.endPosition = stretcher.position ();
	return result;
}

/** Frequency, from positive-going zero crossings. Blunt, and exactly
    right for a sine: the count of times the signal crosses upwards is the
    count of cycles, whatever happened to the phase in between. */
double measuredFrequency (const std::vector<double>& signal)
{
	if (signal.size () < 2)
		return 0.0;

	int crossings = 0;
	for (std::size_t i = 1; i < signal.size (); ++i)
		if (signal[i - 1] <= 0.0 && signal[i] > 0.0)
			++crossings;

	return crossings * kRate / static_cast<double> (signal.size ());
}

double rms (const std::vector<double>& signal)
{
	if (signal.empty ())
		return 0.0;

	double sum = 0.0;
	for (double value : signal)
		sum += value * value;
	return std::sqrt (sum / static_cast<double> (signal.size ()));
}

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - tempo fit tests\n");

	//--------------------------------------------------------------------
	section ("1. The speed a fit asks for");
	//--------------------------------------------------------------------
	{
		// The example from the request itself: a 100 BPM file in a 90 BPM
		// project plays at 0.9 - slower and longer.
		check (close (fitSpeed (100.0, 90.0), 0.9, 1e-12), "100 BPM in a 90 BPM project is 0.9x");
		check (close (fitSpeed (90.0, 100.0), 100.0 / 90.0, 1e-12), "and the other way round is faster");
		check (close (fitSpeed (120.0, 120.0), 1.0, 1e-12), "a matching file is not touched at all");

		// EITHER unknown means no fit. This is the rule that keeps an
		// undetected file playing as it arrived.
		check (close (fitSpeed (0.0, 120.0), 1.0, 1e-12),   "an unknown file tempo is 1.0");
		check (close (fitSpeed (120.0, 0.0), 1.0, 1e-12),   "an unknown project tempo is 1.0");
		check (close (fitSpeed (-90.0, 120.0), 1.0, 1e-12), "and so is a nonsense one");

		// Clamped, because a ratio past two octaves is a detection
		// failure rather than a tempo difference.
		check (close (fitSpeed (10.0, 400.0), kMaxFitSpeed, 1e-12), "an absurd ratio is clamped up");
		check (close (fitSpeed (400.0, 10.0), kMinFitSpeed, 1e-12), "and clamped down");
		check (! close (fitSpeed (10.0, 400.0), 40.0, 1e-9),
		       "NEGATIVE CONTROL: 40x is not passed through");
	}

	//--------------------------------------------------------------------
	section ("2. The mode names and their indices");
	//--------------------------------------------------------------------
	{
		bool roundTrips = true;
		bool named = true;
		for (int i = 0; i < kFitModeCount; ++i)
		{
			const FitMode mode = fitModeFromIndex (i);
			roundTrips &= (indexOfFitMode (mode) == i);

			const char* longName = fitModeName (mode);
			const char* shortName = fitModeShortName (mode);
			named &= (longName != nullptr && longName[0] != '\0');
			named &= (shortName != nullptr && std::string (shortName).size () == 3);
		}
		check (roundTrips, "every mode index round-trips");
		check (named, "and every mode has a name and a three-letter label");

		// Out of range is Off, not undefined behaviour: these come from a
		// parameter, and a host may send anything.
		check (fitModeFromIndex (-1) == FitMode::Off, "a negative index is Off");
		check (fitModeFromIndex (99) == FitMode::Off, "and so is one past the end");
	}

	//--------------------------------------------------------------------
	section ("3. Varispeed moves the pitch");
	//--------------------------------------------------------------------
	{
		const int frames = 44100;                       // one second
		const std::vector<float> source = sineLoop (441.0, frames);

		const Rendered plain = render (source, frames, 1.0, FitMode::Varispeed, frames);
		check (close (measuredFrequency (plain.left), 441.0, 1.0),
		       "at 1.0x the file comes out at its own pitch");

		// 0.9x: slower, longer, and LOWER - which is the whole character
		// of varispeed and the reason the other mode exists.
		const Rendered slower = render (source, frames, 0.9, FitMode::Varispeed, frames);
		check (close (measuredFrequency (slower.left), 441.0 * 0.9, 2.0),
		       "at 0.9x it comes out 10 percent lower");
		check (! close (measuredFrequency (slower.left), 441.0, 5.0),
		       "NEGATIVE CONTROL: varispeed does not keep the pitch");

		// And it takes proportionally longer to get through the file.
		check (close (slower.endPosition, 0.9 * frames, 2.0),
		       "and after a second it is only nine tenths of the way through");
	}

	//--------------------------------------------------------------------
	section ("4. The stretcher keeps the pitch");
	//--------------------------------------------------------------------
	{
		const int frames = 44100;
		const std::vector<float> source = sineLoop (441.0, frames);

		const Rendered kept = render (source, frames, 0.9, FitMode::PitchPreserved, frames);

		// THE MEASUREMENT THAT MATTERS. Same tempo change as section 3,
		// and the frequency has to be the file's own.
		check (close (measuredFrequency (kept.left), 441.0, 2.0),
		       "at 0.9x the pitch is unchanged");
		check (! close (measuredFrequency (kept.left), 441.0 * 0.9, 2.0),
		       "NEGATIVE CONTROL: it is not just varispeed under another name");

		// The MUSICAL position still moves at the fitted speed - the pad's
		// progress bar reads this, and it has to mean the same thing in
		// both modes.
		check (close (kept.endPosition, 0.9 * frames, 2.0),
		       "while the musical position still moves at 0.9x");

		// The splices must not punch holes in it. A sine's RMS is
		// 0.7071; a stretcher that faded to silence at every join, or
		// that doubled through one, would show up here.
		check (close (rms (kept.left), 0.7071, 0.03),
		       "and the level holds through the splices");

		// Faster, too - the jumps go the other way and the same two
		// facts have to hold.
		const Rendered faster = render (source, frames, 1.25, FitMode::PitchPreserved, frames);
		check (close (measuredFrequency (faster.left), 441.0, 2.0),
		       "speeding up keeps the pitch as well");
		check (close (faster.endPosition, std::fmod (1.25 * frames, (double) frames), 3.0),
		       "and gets a quarter further through the file");
		check (close (rms (faster.left), 0.7071, 0.03), "at the same level");
	}

	//--------------------------------------------------------------------
	section ("5. A pad that is not being fitted is untouched");
	//--------------------------------------------------------------------
	{
		const int frames = 44100;
		const std::vector<float> source = sineLoop (441.0, frames);

		// Off IGNORES the speed. A pad switched off must not stretch
		// however wrong its file's tempo is.
		const Rendered off = render (source, frames, 0.5, FitMode::Off, 4096);
		const Rendered unity = render (source, frames, 1.0, FitMode::Varispeed, 4096);

		bool identical = (off.left.size () == unity.left.size ());
		for (std::size_t i = 0; identical && i < off.left.size (); ++i)
			identical = (off.left[i] == unity.left[i]);
		check (identical, "Off at 0.5x is bit-identical to unity");

		// AND SO IS THE STRETCHER AT 1.0. No drift means no splice, which
		// means the sample-accurate path the DSP has always had.
		const Rendered keptUnity = render (source, frames, 1.0, FitMode::PitchPreserved, 4096);
		bool sameAsUnity = (keptUnity.left.size () == unity.left.size ());
		for (std::size_t i = 0; sameAsUnity && i < keptUnity.left.size (); ++i)
			sameAsUnity = (keptUnity.left[i] == unity.left[i]);
		check (sameAsUnity, "and so is the stretcher at 1.0x - no splices fire");

		// The control for that control: at a speed that is NOT 1.0 the
		// stretcher does something different, or the test above would
		// pass on a stretcher that had been switched off by accident.
		const Rendered keptSlow = render (source, frames, 0.9, FitMode::PitchPreserved, 4096);
		bool differs = false;
		for (std::size_t i = 0; i < keptSlow.left.size (); ++i)
			differs |= (keptSlow.left[i] != unity.left[i]);
		check (differs, "NEGATIVE CONTROL: at 0.9x it is doing something");
	}

	//--------------------------------------------------------------------
	section ("6. Files the stretcher cannot splice");
	//--------------------------------------------------------------------
	{
		// Shorter than two overlaps and their search windows: there is
		// nowhere to put a splice, so it falls back to varispeed rather
		// than splicing the file into itself.
		const int frames = 256;
		const std::vector<float> source = sineLoop (441.0, frames);

		const Rendered kept = render (source, frames, 0.5, FitMode::PitchPreserved, 2048);
		const Rendered vari = render (source, frames, 0.5, FitMode::Varispeed, 2048);

		bool identical = true;
		for (std::size_t i = 0; identical && i < kept.left.size (); ++i)
			identical = (kept.left[i] == vari.left[i]);
		check (identical, "a file too short to splice falls back to varispeed");

		// It still gets the LENGTH right, which is what was asked for.
		check (close (kept.endPosition, vari.endPosition, 1e-9),
		       "and still ends up where the tempo says it should");
	}

	//--------------------------------------------------------------------
	section ("7. The playhead itself");
	//--------------------------------------------------------------------
	{
		const int frames = 44100;
		const std::vector<float> source = sineLoop (441.0, frames);

		TimeStretcher stretcher;
		stretcher.setSampleRate (kRate);
		check (close (stretcher.position (), 0.0, 1e-12), "a new stretcher starts at frame 0");

		double l = 0.0, r = 0.0;
		for (int i = 0; i < 1000; ++i)
			stretcher.next (source.data (), frames, 1.0, 1.0, FitMode::Varispeed, l, r);
		check (close (stretcher.position (), 1000.0, 1e-9), "and advances one frame a sample");

		stretcher.setPosition (0.0);
		check (close (stretcher.position (), 0.0, 1e-12), "setPosition puts it back to the start");

		// The RESAMPLING STEP is a separate fact from the tempo fit. A
		// 48 k file in a 44.1 k session steps at 1.088, and a tempo fit
		// of 0.9 on top of that is 0.979 - not either one alone.
		stretcher.reset ();
		const double rateStep = 48000.0 / 44100.0;
		for (int i = 0; i < 1000; ++i)
			stretcher.next (source.data (), frames, rateStep, 0.9, FitMode::Varispeed, l, r);
		check (close (stretcher.position (), 1000.0 * rateStep * 0.9, 1e-6),
		       "rate and tempo multiply, and neither is dropped");
		check (! close (stretcher.position (), 1000.0 * 0.9, 1.0),
		       "NEGATIVE CONTROL: the sample rate ratio is not ignored");

		// Wrapping. A position past the end comes back to the start
		// rather than reading off the buffer.
		stretcher.setPosition (frames - 10.0);
		for (int i = 0; i < 100; ++i)
			stretcher.next (source.data (), frames, 1.0, 1.0, FitMode::Varispeed, l, r);
		check (stretcher.position () >= 0.0 && stretcher.position () < frames,
		       "the playhead wraps at the loop point");
		check (close (stretcher.position (), 90.0, 1e-9), "to exactly where it should be");

		// A null or empty file is silence, not a crash: a slot can be
		// emptied while its voice is running.
		stretcher.next (nullptr, frames, 1.0, 1.0, FitMode::Varispeed, l, r);
		check (l == 0.0 && r == 0.0, "a null source is silence");
		stretcher.next (source.data (), 0, 1.0, 1.0, FitMode::Varispeed, l, r);
		check (l == 0.0 && r == 0.0, "and so is an empty one");
	}

	//--------------------------------------------------------------------
	section ("8. Saying when the file came round again");
	//--------------------------------------------------------------------
	{
		// This is what a ONE-SHOT pad listens to: the end of the file is
		// the moment the playhead wraps, and nothing else knows where
		// that is.
		const int frames = 1000;
		const std::vector<float> source = sineLoop (441.0, frames);

		TimeStretcher stretcher;
		stretcher.setSampleRate (kRate);

		double l = 0.0, r = 0.0;
		check (! stretcher.takeWrapped (), "a new stretcher has not wrapped");

		for (int i = 0; i < 999; ++i)
			stretcher.next (source.data (), frames, 1.0, 1.0, FitMode::Varispeed, l, r);
		check (! stretcher.takeWrapped (), "nor has one that is one sample short");

		stretcher.next (source.data (), frames, 1.0, 1.0, FitMode::Varispeed, l, r);
		check (stretcher.takeWrapped (), "the sample that passes the end reports it");

		// READ AND CLEAR, so the wrap is acted on once. A flag left
		// standing would stop the next pad to look at it.
		check (! stretcher.takeWrapped (), "and asking twice says no the second time");

		// IT IS THE MUSICAL POSITION THAT WRAPS, not the read head. In
		// the pitch-preserving mode the read head jumps about; the file
		// has finished when the MUSIC has, and only once per pass.
		stretcher.reset ();
		int wraps = 0;
		for (int i = 0; i < 4000; ++i)
		{
			stretcher.next (source.data (), frames, 1.0, 0.5, FitMode::PitchPreserved, l, r);
			wraps += stretcher.takeWrapped () ? 1 : 0;
		}
		check (wraps == 2, "at half speed, four thousand samples is two passes");
		check (wraps != 4, "NEGATIVE CONTROL: and not four, which is what the READ head did");

		// setPosition and reset both clear it: a pad relaunched from the
		// top has not just finished.
		stretcher.reset ();
		for (int i = 0; i < 1001; ++i)
			stretcher.next (source.data (), frames, 1.0, 1.0, FitMode::Varispeed, l, r);
		stretcher.setPosition (0.0);
		check (! stretcher.takeWrapped (), "relaunching a pad clears a pending wrap");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
