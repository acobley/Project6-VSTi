//------------------------------------------------------------------------
// Project6 - parameter table tests
//
// A SECOND suite, and it exists because of a bug the first one could not
// possibly have caught.
//
// DspTests.cpp is deliberately free of SDK types, which is what lets it run
// anywhere - but it means it cannot see Project6Params.h at all, and the
// parameter table is where ids, ranges and the predicates that classify
// them live. In VocalFilter a predicate whose upper bound was "the end of
// the table" survived one append and quietly misclassified the parameter
// that had just been added: the processor refused to record it and the
// editor refused to redraw for it, so the control wrote its parameter, the
// host saw the write, and nothing happened.
//
// This file needs only the SDK's HEADERS - vsttypes.h is typedefs - so it
// links against Project6Params.cpp and nothing else:
//
//     c++ -std=c++17 -O2 -Isource -Iexternal/vst3sdk
//         tests/ParamsTests.cpp source/Project6Params.cpp
//         source/Project6Dsp.cpp -o /tmp/paramstests && /tmp/paramstests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//------------------------------------------------------------------------

#include "Project6Params.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace Project6;
using Steinberg::Vst::ParamID;

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

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - parameter table tests\n");

	//--------------------------------------------------------------------
	section ("1. The table describes itself honestly");
	//--------------------------------------------------------------------
	{
		bool idsMatch = true, titlesPresent = true, unitsPresent = true, rangesSane = true;

		for (ParamID id = 0; id < kNumParams; ++id)
		{
			const ParamDef& def = kParams[id];

			// A row in the wrong place is the one mistake this table can
			// make that nothing else notices: every lookup is by index.
			idsMatch &= (def.id == id);

			// NEVER null. RangeParameter dereferences both without a
			// check, and the symptom is the VALIDATOR segfaulting in the
			// post-build step rather than anything pointing at the table.
			titlesPresent &= (def.title != nullptr && def.title[0] != '\0');
			unitsPresent  &= (def.units != nullptr);

			rangesSane &= (def.plainMax > def.plainMin);
			rangesSane &= (def.plainDefault >= def.plainMin && def.plainDefault <= def.plainMax);
		}

		check (idsMatch,       "every row's id equals its index");
		check (titlesPresent,  "no title is null or empty");
		check (unitsPresent,   "no units string is null (empty is fine, null is not)");
		check (rangesSane,     "every range is ordered and contains its default");
	}

	//--------------------------------------------------------------------
	section ("2. The mappings, at both ends and in the middle");
	//--------------------------------------------------------------------
	{
		bool ends = true, middles = true, roundTrips = true;

		for (ParamID id = 0; id < kNumParams; ++id)
		{
			const ParamDef& def = kParams[id];

			ends &= close (def.toPlain (0.0), def.plainMin, 1e-9);
			ends &= close (def.toPlain (1.0), def.plainMax, 1e-9);
			middles &= close (def.toPlain (0.5),
			                  (def.plainMin + def.plainMax) * 0.5, 1e-9);

			// normalised -> plain -> normalised, across the travel.
			for (int step = 0; step <= 20; ++step)
			{
				const double n = step / 20.0;
				roundTrips &= close (def.toNormalized (def.toPlain (n)), n, 1e-9);
			}
		}

		check (ends,       "0 and 1 map to the ends of the plain range");
		check (middles,    "0.5 maps to the middle");
		check (roundTrips, "normalised -> plain -> normalised round-trips");
	}

	//--------------------------------------------------------------------
	section ("3. internal == plain, and saying so out loud");
	//--------------------------------------------------------------------
	{
		// TRUE FOR NOW, AND ONLY FOR NOW. There is no DXi behind this
		// plug-in, so nothing has a MapToInternal to reproduce. When the
		// first one arrives this check is what tells you the slot you are
		// filling in was genuinely empty rather than being one you have
		// just broken - so change it deliberately, do not delete it.
		bool identical = true;
		for (ParamID id = 0; id < kNumParams; ++id)
		{
			const ParamDef& def = kParams[id];
			for (int step = 0; step <= 20; ++step)
			{
				const double n = step / 20.0;
				identical &= close (def.toInternal (n), def.toPlain (n), 1e-9);
			}
		}
		check (identical, "every parameter's internal range equals its plain range");
	}

	//--------------------------------------------------------------------
	section ("4. The lookups, and the id that is not in the table");
	//--------------------------------------------------------------------
	{
		check (&paramDef (kOutputTrim) == &kParams[kOutputTrim],
		       "paramDef returns the row it is asked for");

		// kBypass is 1000. Indexing kParams with it would read past the
		// end of the array; paramDef must range-check instead.
		check (&paramDef (kBypass) == &kParams[kOutputTrim],
		       "paramDef (kBypass) is range-checked, not read past the end");
		check (&paramDef (kNumParams) == &kParams[kOutputTrim],
		       "so is one past the end of the table");

		check (isTableParam (kOutputTrim),  "the trim is a table parameter");
		check (! isTableParam (kBypass),    "kBypass is not");
		check (kBypass > kNumParams,        "and it is past the end, as the convention requires");
	}

	//--------------------------------------------------------------------
	section ("5. The state boundary");
	//--------------------------------------------------------------------
	{
		// The processor and the controller both write and read exactly
		// kNumStoredParams doubles. While there are no published values it
		// equals kNumParams - and when the first one is appended, THIS is
		// the line that has to move with it.
		check (kNumStoredParams == kNumParams,
		       "every parameter is currently a saved setting");
		check (kNumStoredParams <= kNumParams,
		       "the saved block never runs past the end of the table");
	}

	//--------------------------------------------------------------------
	section ("6. The table agrees with the DSP it feeds");
	//--------------------------------------------------------------------
	{
		const ParamDef& trim = kParams[kOutputTrim];

		// The ranges come from Project6Dsp.h and must not have been
		// retyped here - that is the whole reason the header owns them.
		check (close (trim.plainMin, kTrimMinDb, 0.0), "the trim's minimum is the DSP's");
		check (close (trim.plainMax, kTrimMaxDb, 0.0), "the trim's maximum is the DSP's");
		check (close (trim.plainDefault, kTrimDefaultDb, 0.0), "so is its default");

		// THE DEFAULT IS UNITY. A default trim above 0 dB would be a boost
		// nothing else in the project mentions.
		check (close (dbToLinear (trim.plainDefault, kTrimMinDb), 1.0, 1e-12),
		       "the default trim is unity gain");
		check (trim.smoothed, "the trim is marked smoothed - it is, in the DSP");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
