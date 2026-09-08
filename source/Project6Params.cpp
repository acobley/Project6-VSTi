//------------------------------------------------------------------------
// Project6 - the parameter table
//
// Every range and default here comes from a constant in Project6Dsp.h or
// Project6Slots.h, so the DSP's idea of what a value may be and the host's
// idea cannot drift apart.
//------------------------------------------------------------------------

#include "Project6Params.h"

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
// The table has to agree with itself. These cost nothing at run time and
// fire at compile time, which is the only place a typo in a table like
// this is cheap to find.
//------------------------------------------------------------------------
static_assert (kNumTableParams == 1, "one described parameter: the output trim");
static_assert (kNumParams == kNumTableParams + kSlotCount,
               "the table plus the trigger block is every parameter there is");
static_assert (kSlotPlayEnd == kNumParams,
               "the trigger block currently runs to the end - if you append after "
               "it, isSlotPlayParam must still be bounded by kSlotPlayEnd");
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

	return kParams[kOutputTrim];
}

//------------------------------------------------------------------------
const char* paramTitle (Steinberg::Vst::ParamID id)
{
	if (id < kNumTableParams)
		return kParams[id].title;

	if (isSlotPlayParam (id))
	{
		// LETTERED BY ROW, NUMBERED BY COLUMN - "Slot C6 Play" - so a name
		// in a host's automation lane says where the pad is on the panel.
		// A flat 1..64 would make the reader do the division.
		//
		// Built once, on first use, and never touched again: ParamDef and
		// the SDK both want a const char*, so the strings have to outlive
		// the call, and sixty-four literals would be sixty-four lines
		// differing in one character.
		static const std::vector<std::string> titles = []
		{
			std::vector<std::string> names;
			names.reserve (kSlotCount);
			for (int slot = 0; slot < kSlotCount; ++slot)
			{
				const int row = slot / kSlotColumns;
				const int column = slot % kSlotColumns;
				names.push_back (std::string ("Slot ")
				                 + static_cast<char> ('A' + row)
				                 + std::to_string (column + 1)
				                 + " Play");
			}
			return names;
		}();

		return titles[static_cast<std::size_t> (slotOfPlayParam (id))].c_str ();
	}

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
