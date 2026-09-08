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
//         source/Project6Dsp.cpp source/Project6Sample.cpp
//         -o /tmp/paramstests && /tmp/paramstests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//------------------------------------------------------------------------

#include "Project6Params.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
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
		bool titlesDistinct = true;

		std::set<std::string> seenTitles;

		for (ParamID id = 0; id < kNumParams; ++id)
		{
			// paramDef, not kParams[id]: the sixty-four triggers share one
			// definition and are not rows in the table.
			const ParamDef& def = paramDef (id);

			// A row in the wrong place is the one mistake the table can
			// make that nothing else notices: every table lookup is by
			// index. The shared trigger definition is exempt - it cannot
			// carry sixty-four ids, and nothing reads its id.
			if (id < kNumTableParams)
				idsMatch &= (def.id == id);

			// NEVER null. RangeParameter dereferences both without a
			// check, and the symptom is the VALIDATOR segfaulting in the
			// post-build step rather than anything pointing at the table.
			const char* title = paramTitle (id);
			titlesPresent &= (title != nullptr && title[0] != '\0');
			unitsPresent  &= (def.units != nullptr);

			// And DISTINCT. Sixty-four parameters called "Slot Play" is a
			// host automation menu nobody can use - which is the whole
			// reason paramTitle exists apart from ParamDef::title.
			if (title != nullptr)
				titlesDistinct &= seenTitles.insert (std::string (title)).second;

			rangesSane &= (def.plainMax > def.plainMin);
			rangesSane &= (def.plainDefault >= def.plainMin && def.plainDefault <= def.plainMax);
		}

		check (idsMatch,       "every table row's id equals its index");
		check (titlesPresent,  "no title is null or empty");
		check (titlesDistinct, "and no two parameters share a name");
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
			const ParamDef& def = paramDef (id);

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
			const ParamDef& def = paramDef (id);

			// A two-state or integer parameter SNAPS or ROUNDS rather
			// than scaling - that is toInternal doing its job, not the
			// ranges differing - so those are checked on their own,
			// below.
			if (def.type == ParamType::Bool || def.type == ParamType::Int
			    || def.type == ParamType::Enum)
				continue;

			for (int step = 0; step <= 20; ++step)
			{
				const double n = step / 20.0;
				identical &= close (def.toInternal (n), def.toPlain (n), 1e-9);
			}
		}
		check (identical, "every continuous parameter's internal range equals its plain range");

		// The snap, at the boundary and either side of it.
		const ParamDef& trigger = slotPlayDef ();
		check (trigger.toInternal (0.0)  == 0.0, "a trigger at 0 is stopped");
		check (trigger.toInternal (0.49) == 0.0, "just under half is still stopped");
		check (trigger.toInternal (0.5)  == 1.0, "half is playing");
		check (trigger.toInternal (1.0)  == 1.0, "and so is 1");

		// And the rounding, which is what the panel relies on to turn a
		// published transport state back into an enum.
		const ParamDef& state = liveTransportDef ();
		check (state.toInternal (state.toNormalized (kTransportUnknown)) == kTransportUnknown,
		       "the transport state round-trips through normalised: unknown");
		check (state.toInternal (state.toNormalized (kTransportStopped)) == kTransportStopped,
		       "stopped");
		check (state.toInternal (state.toNormalized (kTransportPlaying)) == kTransportPlaying,
		       "and playing");

		const ParamDef& beats = liveBeatsPerBarDef ();
		bool beatsRoundTrip = true;
		for (int n = 1; n <= kMaxBeatsPerBar; ++n)
			beatsRoundTrip &= (static_cast<int> (beats.toInternal (beats.toNormalized (n))) == n);
		check (beatsRoundTrip, "and every bar length from 1 to 32 does too");
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

		check (&paramDef (slotPlayParam (0)) == &slotPlayDef (),
		       "every trigger resolves to the one shared definition");
		check (&paramDef (slotPlayParam (kSlotCount - 1)) == &slotPlayDef (),
		       "including the last one");

		check (isPluginParam (kOutputTrim),  "the trim is one of this plug-in's parameters");
		check (isPluginParam (slotPlayParam (kSlotCount - 1)), "so is the last trigger");
		check (! isPluginParam (kBypass),    "kBypass is not");
		check (kBypass > kNumParams,         "and it is past the end, as the convention requires");
	}

	//--------------------------------------------------------------------
	section ("5. The state boundary");
	//--------------------------------------------------------------------
	{
		// The processor and the controller both write and read exactly
		// kNumStoredParams doubles.
		check (kNumStoredParams == kNumTableParams,
		       "only the trim is saved");
		check (kNumStoredParams < kNumParams,
		       "the triggers are deliberately outside the saved block");
		check (kNumStoredParams <= kNumParams,
		       "and the saved block never runs past the end");

		// A project that reopened with six pads looping would be a
		// project nobody could open quietly. The triggers stay out of the
		// stream, and BOTH sides reset every parameter past the saved
		// block to its default before reading - which only works if the
		// default is "stopped".
		check (slotPlayDef ().plainDefault == 0.0,
		       "a trigger defaults to stopped, so a loaded project is quiet");
		check (slotPlayDef ().defaultNormalized () == 0.0,
		       "in normalised terms too");
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
	section ("7. The trigger block, and the bound that is not kNumParams");
	//--------------------------------------------------------------------
	{
		check (kSlotPlayBase == kOutputTrim + 1, "the block is APPENDED after the trim");
		check (kSlotPlayEnd - kSlotPlayBase == kSlotCount, "one trigger per slot");
		check (kNumParams > kSlotPlayEnd,
		       "and something is now appended AFTER it - which is what makes the "
		       "bound below load-bearing");

		// The two directions have to agree for every slot, not just for
		// one - an off-by-one here plays the wrong pad.
		bool roundTrips = true, classified = true;
		for (int slot = 0; slot < kSlotCount; ++slot)
		{
			const ParamID id = slotPlayParam (slot);
			roundTrips &= (slotOfPlayParam (id) == slot);
			classified &= isSlotPlayParam (id);
		}
		check (roundTrips, "slot -> parameter -> slot round-trips for all 64");
		check (classified, "and all 64 are classified as triggers");

		check (! isSlotPlayParam (kOutputTrim), "the trim is not one");
		check (! isSlotPlayParam (kBypass),     "and neither is the bypass");
		check (! isSlotPlayParam (kSlotPlayEnd),
		       "NEGATIVE CONTROL: one past the block is not in the block");

		// THE TRAP THIS WHOLE SECTION IS ABOUT. VocalFilter's equivalent
		// predicate was bounded by the end of the TABLE rather than by the
		// end of its own block. It survived one append and then
		// misclassified the parameter added after it, and the symptom was
		// a control that wrote its parameter, was seen by the host, and
		// did nothing. If a parameter is ever appended after the triggers,
		// this check fails and says why.
		check (isSlotPlayParam (kSlotPlayEnd - 1) && !isSlotPlayParam (kSlotPlayEnd),
		       "the block is bounded by kSlotPlayEnd, not by kNumParams");

		// Names that say where the pad is.
		check (std::string (paramTitle (slotPlayParam (0)))  == "Slot A1 Play",
		       "slot 0 is named for the top-left cell");
		check (std::string (paramTitle (slotPlayParam (7)))  == "Slot A8 Play",
		       "slot 7 ends the first row");
		check (std::string (paramTitle (slotPlayParam (8)))  == "Slot B1 Play",
		       "slot 8 starts the second");
		check (std::string (paramTitle (slotPlayParam (63))) == "Slot H8 Play",
		       "and slot 63 is the bottom-right cell");

		check (std::string (paramChoiceName (slotPlayParam (0), 0)) == "Stopped",
		       "a host's list reads Stopped");
		check (std::string (paramChoiceName (slotPlayParam (0), 1)) == "Playing",
		       "and Playing, rather than 0 and 1");
	}

	//--------------------------------------------------------------------
	section ("8. The published block");
	//--------------------------------------------------------------------
	{
		check (kLiveBase == kSlotPlayEnd, "it starts where the triggers end, with no hole");
		check (kLiveEnd == kNumParams,    "and currently runs to the end");
		check (kLiveSlotEnd - kLiveSlotBase == kSlotCount, "one published state per slot");

		// The enum trap this block was written into: an enumerator after
		// an explicitly valued one carries on from THAT value, so
		// kLiveTransport had to be anchored to kSlotPlayEnd by hand. If
		// it is ever left bare, this is the check that notices the hole.
		check (kLiveTransport == kSlotPlayEnd, "the first published id is not one past the gap");
		check (kLiveBarPhase == kLiveTransport + 1, "and they are contiguous");
		check (kLiveBeatsPerBar == kLiveBarPhase + 1, "all the way");
		check (kLiveSlotBase == kLiveBeatsPerBar + 1, "to the per-slot block");

		bool roundTrips = true, classified = true, notTriggers = true;
		for (int slot = 0; slot < kSlotCount; ++slot)
		{
			const ParamID id = liveSlotParam (slot);
			roundTrips  &= (slotOfLiveParam (id) == slot);
			classified  &= isLiveParam (id) && isLiveSlotParam (id);
			// THE WHOLE POINT OF THE BOUND. Every one of these ids is
			// past kSlotPlayEnd, so a predicate reading `id < kNumParams`
			// would classify all sixty-four of them as triggers.
			notTriggers &= ! isSlotPlayParam (id);
		}
		check (roundTrips,  "slot -> published id -> slot round-trips for all 64");
		check (classified,  "and all 64 are classified as published");
		check (notTriggers, "NEGATIVE CONTROL: and none of them as a trigger");

		check (isLiveParam (kLiveTransport) && isLiveParam (kLiveBarPhase)
		           && isLiveParam (kLiveBeatsPerBar),
		       "the three single published values are in the block");
		check (! isLiveParam (kOutputTrim), "the trim is not");
		check (! isLiveParam (kNumParams),  "and neither is one past the end");

		// They are NOT saved, and both sides reset them before reading.
		check (kNumStoredParams < kLiveBase, "nothing published is in the saved block");

		// Names, so a debugger and a host's own list are readable.
		check (std::string (paramTitle (liveSlotParam (0)))  == "Slot A1 Sounding",
		       "a published slot state is named for its cell");
		check (std::string (paramTitle (liveSlotParam (63))) == "Slot H8 Sounding",
		       "at both ends");
		check (std::string (paramTitle (liveSlotParam (0)))
		           != std::string (paramTitle (slotPlayParam (0))),
		       "and is not the same name as its trigger");
		check (std::string (paramTitle (kLiveTransport)) == "Transport", "the transport");
		check (std::string (paramTitle (kLiveBeatsPerBar)) == "Beats Per Bar",
		       "and the bar length have names too");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
