//------------------------------------------------------------------------
// Project6 - parameter definitions
//
// One table, read by BOTH the processor and the controller, so a range or
// a default cannot be typed twice and disagree.
//
// Every parameter carries THREE ranges, which is the shape the SpaceDub,
// ForTran, SpyBand and VocalFilter ports all settled on:
//
//   * VST3 normalised, 0..1, which is what the host works in;
//   * "plain", what the panel and the host display;
//   * "internal", what the DSP is actually handed, via toInternal().
//
// There is no DXi behind this plug-in, so nothing here has a
// ParamInfo::MapToInternal to reproduce and INTERNAL == PLAIN throughout.
// The slot is empty, not absent: when the first ported control arrives,
// its mapping goes into toInternal() and the two ranges part company
// there and nowhere else.
//
// APPEND NEW PARAMETERS, NEVER INSERT. An id that moves loads a saved
// project's value into the wrong control.
//------------------------------------------------------------------------

#pragma once

#include "Project6Dsp.h"
#include "Project6Slots.h"

#include "pluginterfaces/vst/vsttypes.h"

namespace Project6 {

//------------------------------------------------------------------------
enum Param : Steinberg::Vst::ParamID
{
	/** The output trim. A real parameter with a real effect, and the last
	    stage of the signal path, which is where it will stay. */
	kOutputTrim,

	//--------------------------------------------------------------------
	// THE SLOT TRIGGERS: sixty-four of them, one per sample slot, each a
	// two-state play/stop.
	//
	// They are PARAMETERS rather than messages on purpose. This project's
	// own rule, written down when the slot paths had to travel as a
	// message: anything that can be expressed as a number is a parameter,
	// because a message can be lost when a host does not connect the two
	// components and a parameter cannot. A play/stop is a number. It also
	// gets automation, host undo and a panel that follows an automation
	// lane for nothing, all through machinery that already exists.
	//
	// The price is sixty-four rows in the host's generic parameter list.
	// That is the honest cost of pads a host can automate.
	//
	// APPENDED after kOutputTrim, which is why the trim is still id 0.
	//--------------------------------------------------------------------
	kSlotPlayBase,

	kNumParams = kSlotPlayBase + kSlotCount
};

/** The trigger for one slot. The only place this arithmetic lives. */
constexpr Steinberg::Vst::ParamID slotPlayParam (int slot)
{
	return static_cast<Steinberg::Vst::ParamID> (kSlotPlayBase + slot);
}

/** One past the last trigger.

    BOUNDED BY THE BLOCK, NOT BY THE TABLE. VocalFilter's equivalent
    predicate read `id < kNumParams`, survived one append, and then
    misclassified the parameter that had just been added after it: the
    processor refused to record it and the editor refused to redraw for
    it, so the control wrote its parameter, the host saw the write, and
    nothing whatsoever happened. A range check whose upper bound is "the
    end of the table" is a bug waiting for the next append. */
constexpr Steinberg::Vst::ParamID kSlotPlayEnd = kSlotPlayBase + kSlotCount;

constexpr bool isSlotPlayParam (Steinberg::Vst::ParamID id)
{
	return id >= kSlotPlayBase && id < kSlotPlayEnd;
}

/** Which slot a trigger belongs to. Only meaningful for a trigger id. */
constexpr int slotOfPlayParam (Steinberg::Vst::ParamID id)
{
	return static_cast<int> (id - kSlotPlayBase);
}

/** The settings, which is what the state stream carries.

    THE TRIGGERS ARE DELIBERATELY OUTSIDE IT. A project that reopened with
    six pads already looping would be a project nobody could open quietly,
    and "what was playing when you saved" is not a setting - it is a
    moment. Automation still restores them, because automation lives in
    the host. */
constexpr Steinberg::Vst::ParamID kNumStoredParams = kSlotPlayBase;

/** How many parameters the table below describes one by one. The triggers
    are a BLOCK - sixty-four definitions identical but for their number -
    and are described once, by slotPlayDef(), rather than as sixty-four
    rows differing in one character each. */
constexpr Steinberg::Vst::ParamID kNumTableParams = kSlotPlayBase;

/** The VST3 bypass, which hosts expect. 1000 is the convention, and it is
    far past the end of everything - so RANGE-CHECK every id before
    indexing. paramDef() below does. */
constexpr Steinberg::Vst::ParamID kBypass = 1000;

//------------------------------------------------------------------------
enum class ParamType { Float, Bool, Enum, Int };

//------------------------------------------------------------------------
struct ParamDef
{
	Steinberg::Vst::ParamID id;
	const char* title;          // NEVER null - RangeParameter dereferences it
	const char* units;          // NEVER null; "" when the value string carries its own
	ParamType   type;
	double      plainMin;
	double      plainMax;
	double      plainDefault;
	double      internalMin;    // range the DSP expects
	double      internalMax;
	int         stepCount;      // 0 = continuous
	bool        smoothed;       // ramped per sample rather than per block

	//--------------------------------------------------------------------
	double toPlain (double normalized) const
	{
		return plainMin + normalized * (plainMax - plainMin);
	}

	double toNormalized (double plain) const
	{
		if (plainMax == plainMin)
			return 0.0;
		return (plain - plainMin) / (plainMax - plainMin);
	}

	/** Reproduces ParamInfo::MapToInternal from the DXi world: booleans
	    snap, enums and integers round, floats scale. With internalMin ==
	    plainMin and internalMax == plainMax - which is the case for every
	    parameter in this plug-in - it returns the plain value. */
	double toInternal (double normalized) const
	{
		if (type == ParamType::Bool)
			return (normalized < 0.5) ? 0.0 : 1.0;

		const double v = internalMin + normalized * (internalMax - internalMin);
		if (type == ParamType::Enum || type == ParamType::Int)
			return static_cast<double> (static_cast<long> (v + 0.5));
		return v;
	}

	double defaultNormalized () const { return toNormalized (plainDefault); }
};

//------------------------------------------------------------------------
/** The individually described parameters. NOT indexable by every id -
    only by an id below kNumTableParams. Use paramDef() instead. */
extern const ParamDef kParams[kNumTableParams];

/** The definition every slot trigger shares. */
const ParamDef& slotPlayDef ();

/** Look a definition up by id, RANGE-CHECKED, table or block. Returns the
    trim's definition for anything unknown - including kBypass, which is
    1000 and is not a parameter of this plug-in's own. */
const ParamDef& paramDef (Steinberg::Vst::ParamID id);

/** What the host calls a parameter. Never null.

    Separate from ParamDef::title because the triggers share one
    definition and cannot share one name: "Slot A1 Play" through
    "Slot H8 Play", lettered by row and numbered by column so the name
    matches where the slot is on the panel. */
const char* paramTitle (Steinberg::Vst::ParamID id);

/** The name of one choice of a two-state or enumerated parameter. Never
    null. */
const char* paramChoiceName (Steinberg::Vst::ParamID id, int choice);

/** True for an id this plug-in defines at all. */
inline bool isPluginParam (Steinberg::Vst::ParamID id) { return id < kNumParams; }

/** The plain value of a parameter, given the whole normalised set. */
inline double plainValue (const double* normalized, Steinberg::Vst::ParamID id)
{
	return paramDef (id).toPlain (normalized[id]);
}

//------------------------------------------------------------------------
} // namespace Project6
