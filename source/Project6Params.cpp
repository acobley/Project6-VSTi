//------------------------------------------------------------------------
// Project6 - the parameter table
//
// Every range and default here comes from a constant in Project6Dsp.h or
// Project6Slots.h, so the DSP's idea of what a value may be and the host's
// idea cannot drift apart.
//------------------------------------------------------------------------

#include "Project6Params.h"

#include <cassert>
#include <string>
#include <vector>

namespace Project6 {

//------------------------------------------------------------------------
// id            title          units type              plainMin     plainMax     plainDefault     internalMin  internalMax  steps smoothed
//------------------------------------------------------------------------
const ParamDef kParams[kNumTableParams] =
{
	// internal == plain, deliberately - see the banner in the header.
	{ kOutputTrim, "Output Trim", "dB", ParamType::Float, kTrimMinDb,  kTrimMaxDb,  kTrimDefaultDb,  kTrimMinDb,  kTrimMaxDb,  0,    true },
};

//------------------------------------------------------------------------
const ParamDef& slotPlayDef ()
{
	// ONE definition for all sixty-four. `id` is kSlotPlayBase because a
	// shared row cannot carry sixty-four ids; nothing reads it, and the
	// two places that would have - paramTitle and the controller's
	// addParameters - are passed the real id instead.
	//
	// DEFAULT 0. A pad that came up playing would be a plug-in that makes
	// noise the moment it is inserted.
	static const ParamDef def =
		{ kSlotPlayBase, "Slot Play", "", ParamType::Bool, 0.0, 1.0, 0.0, 0.0, 1.0, 1, false };
	return def;
}

//------------------------------------------------------------------------
const ParamDef& slotLevelDef ()
{
	// -40 to +12 dB, from Project6Dsp.h, so the DSP's idea of a level and
	// the host's cannot drift apart. LINEAR IN DECIBELS, which is what
	// makes the validator's round trip through getParamStringByValue /
	// getParamValueByString exact without a toString override.
	static const ParamDef def =
		{ kSlotLevelBase, "Slot Level", "dB", ParamType::Float,
		  kSlotLevelMinDb, kSlotLevelMaxDb, kSlotLevelDefaultDb,
		  kSlotLevelMinDb, kSlotLevelMaxDb, 0, true };
	return def;
}

//------------------------------------------------------------------------
const ParamDef& rowLevelDef ()
{
	static const ParamDef def =
		{ kRowLevelBase, "Row Level", "dB", ParamType::Float,
		  kRowLevelMinDb, kRowLevelMaxDb, kRowLevelDefaultDb,
		  kRowLevelMinDb, kRowLevelMaxDb, 0, true };
	return def;
}

//------------------------------------------------------------------------
const ParamDef& liveTransportDef ()
{
	// An integer rather than an enumerated parameter: it is hidden, so no
	// host ever renders its choices, and a StringListParameter would buy
	// three strings nobody reads.
	static const ParamDef def =
		{ kLiveTransport, "Transport", "", ParamType::Int,
		  0.0, static_cast<double> (kTransportStates - 1), 0.0,
		  0.0, static_cast<double> (kTransportStates - 1), kTransportStates - 1, false };
	return def;
}

//------------------------------------------------------------------------
const ParamDef& liveBarPhaseDef ()
{
	static const ParamDef def =
		{ kLiveBarPhase, "Bar Phase", "", ParamType::Float,
		  0.0, 1.0, 0.0, 0.0, 1.0, 0, false };
	return def;
}

//------------------------------------------------------------------------
const ParamDef& liveBeatsPerBarDef ()
{
	// The panel draws one vertical line per beat, so it needs the
	// numerator - a 3/4 bar ruled into four is worse than no ruling at
	// all. Published rather than assumed for exactly that reason.
	static const ParamDef def =
		{ kLiveBeatsPerBar, "Beats Per Bar", "", ParamType::Int,
		  1.0, static_cast<double> (kMaxBeatsPerBar), 4.0,
		  1.0, static_cast<double> (kMaxBeatsPerBar), kMaxBeatsPerBar - 1, false };
	return def;
}

//------------------------------------------------------------------------
const ParamDef& liveSlotDef ()
{
	static const ParamDef def =
		{ kLiveSlotBase, "Slot Sounding", "", ParamType::Bool,
		  0.0, 1.0, 0.0, 0.0, 1.0, 1, false };
	return def;
}

//------------------------------------------------------------------------
namespace {

/** "A1" .. "H8" - the grid reference of a slot, lettered by row and
    numbered by column, so a parameter name says where the pad is on the
    panel rather than making the reader divide by eight. */
std::string slotReference (int slot)
{
	const int row = slot / kSlotColumns;
	const int column = slot % kSlotColumns;
	return std::string (1, static_cast<char> ('A' + row)) + std::to_string (column + 1);
}

/** Built once, on first use, and never touched again: ParamDef and the
    SDK both want a const char*, so the strings have to outlive the call,
    and sixty-four literals would be sixty-four lines differing in one
    character. */
const std::vector<std::string>& slotNames (const char* suffix)
{
	static std::vector<std::string> play;
	static std::vector<std::string> sounding;
	static std::vector<std::string> level;

	// Keyed on the first letter, which is unique across the three
	// suffixes this is ever called with. A fourth would need a real key -
	// hence the assertion, which fires the moment one is added.
	std::vector<std::string>& names = (suffix[0] == 'P') ? play
	                                : (suffix[0] == 'S') ? sounding
	                                                     : level;
	assert (suffix[0] == 'P' || suffix[0] == 'S' || suffix[0] == 'L');
	if (names.empty ())
	{
		names.reserve (kSlotCount);
		for (int slot = 0; slot < kSlotCount; ++slot)
			names.push_back ("Slot " + slotReference (slot) + " " + suffix);
	}
	return names;
}

/** "Row A Level" .. "Row H Level", lettered to match the first half of a
    slot's own name: slot C6 is on row C, and this is that row's fader. */
const std::vector<std::string>& rowNames ()
{
	static std::vector<std::string> names;
	if (names.empty ())
	{
		names.reserve (kSlotRows);
		for (int row = 0; row < kSlotRows; ++row)
			names.push_back (std::string ("Row ")
			                 + static_cast<char> ('A' + row) + " Level");
	}
	return names;
}

} // namespace

//------------------------------------------------------------------------
// The table has to agree with itself. These cost nothing at run time and
// fire at compile time, which is the only place a typo in a table like
// this is cheap to find.
//------------------------------------------------------------------------
static_assert (kNumTableParams == 1, "one described parameter: the output trim");
static_assert (kNumParams == kNumTableParams + kSlotCount + 3 + kSlotCount + kSlotCount
                                 + kSlotRows,
               "the trim, 64 triggers, the transport, the bar phase, the beats per "
               "bar, 64 published slot states, 64 slot levels and 8 row levels is "
               "every parameter there is");
// THE APPEND THE BOUND WAS WRITTEN FOR. The published block now sits
// after the triggers, so isSlotPlayParam's upper bound is load-bearing
// rather than merely careful.
static_assert (kSlotPlayEnd < kNumParams,
               "something follows the trigger block, so isSlotPlayParam must be "
               "bounded by kSlotPlayEnd and never by kNumParams");
static_assert (kLiveBase == kSlotPlayEnd, "the published block starts where the triggers end");
static_assert (kLiveEnd < kNumParams,
               "the level block follows the published one, so isLiveParam must be "
               "bounded by kLiveEnd and never by kNumParams");
static_assert (kSlotLevelBase == kLiveEnd, "the levels start where the published block ends");
static_assert (kSlotLevelEnd < kNumParams,
               "the row levels follow the slot levels, so isSlotLevelParam must be "
               "bounded by kSlotLevelEnd and never by kNumParams");
static_assert (kRowLevelBase == kSlotLevelEnd, "the row levels start where they end");
static_assert (kRowLevelEnd == kNumParams, "and currently run to the end");
static_assert (kRowLevelEnd - kRowLevelBase == kSlotRows, "one fader per row");
static_assert (! isSlotLevelParam (rowLevelParam (0)), "a row level is not a slot level");
static_assert (rowOfLevelParam (rowLevelParam (5)) == 5, "the two directions agree");
static_assert (! isLiveParam (slotLevelParam (0)), "a level is not a published value");
static_assert (! isSlotPlayParam (slotLevelParam (0)), "nor a trigger");
static_assert (slotOfLevelParam (slotLevelParam (7)) == 7, "the two directions agree");
static_assert (! isSlotPlayParam (kLiveTransport), "a published id is not a trigger");
static_assert (! isLiveParam (slotPlayParam (kSlotCount - 1)), "and a trigger is not published");
static_assert (isLiveParam (kLiveTransport) && isLiveParam (kLiveSlotEnd - 1),
               "the published block covers its own ends");
static_assert (slotOfLiveParam (liveSlotParam (42)) == 42, "the two directions agree");
static_assert (kNumStoredParams == kNumTableParams,
               "only the trim is saved; the triggers are a moment, not a setting");
static_assert (kBypass > kNumParams,
               "kBypass must be past the end, so indexing it is caught");
static_assert (slotPlayParam (0) == kSlotPlayBase, "slot 0's trigger heads the block");
static_assert (slotOfPlayParam (slotPlayParam (17)) == 17, "the two directions agree");
static_assert (isSlotPlayParam (slotPlayParam (kSlotCount - 1)), "the last one is in the block");
static_assert (! isSlotPlayParam (kOutputTrim), "and the trim is not");
static_assert (! isSlotPlayParam (kBypass), "and neither is the bypass");

//------------------------------------------------------------------------
const ParamDef& paramDef (Steinberg::Vst::ParamID id)
{
	if (id < kNumTableParams)
		return kParams[id];

	if (isSlotPlayParam (id))
		return slotPlayDef ();

	if (id == kLiveTransport)
		return liveTransportDef ();
	if (id == kLiveBarPhase)
		return liveBarPhaseDef ();
	if (id == kLiveBeatsPerBar)
		return liveBeatsPerBarDef ();
	if (isLiveSlotParam (id))
		return liveSlotDef ();
	if (isSlotLevelParam (id))
		return slotLevelDef ();
	if (isRowLevelParam (id))
		return rowLevelDef ();

	return kParams[kOutputTrim];
}

//------------------------------------------------------------------------
const char* paramTitle (Steinberg::Vst::ParamID id)
{
	if (id < kNumTableParams)
		return kParams[id].title;

	// LETTERED BY ROW, NUMBERED BY COLUMN - "Slot C6 Play" - so a name in
	// a host's automation lane says where the pad is on the panel.
	if (isSlotPlayParam (id))
		return slotNames ("Play")[static_cast<std::size_t> (slotOfPlayParam (id))].c_str ();

	if (isLiveSlotParam (id))
		return slotNames ("Sounding")[static_cast<std::size_t> (slotOfLiveParam (id))].c_str ();

	if (isSlotLevelParam (id))
		return slotNames ("Level")[static_cast<std::size_t> (slotOfLevelParam (id))].c_str ();

	if (isRowLevelParam (id))
		return rowNames ()[static_cast<std::size_t> (rowOfLevelParam (id))].c_str ();

	if (id == kLiveTransport)
		return liveTransportDef ().title;
	if (id == kLiveBarPhase)
		return liveBarPhaseDef ().title;
	if (id == kLiveBeatsPerBar)
		return liveBeatsPerBarDef ().title;

	if (id == kBypass)
		return "Bypass";

	return "Parameter";
}

//------------------------------------------------------------------------
const char* paramChoiceName (Steinberg::Vst::ParamID id, int choice)
{
	if (isSlotPlayParam (id))
		return (choice == 0) ? "Stopped" : "Playing";

	return (choice == 0) ? "Off" : "On";
}

//------------------------------------------------------------------------
} // namespace Project6
