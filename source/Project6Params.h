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
// THERE IS NOTHING BEHIND THIS PLUG-IN YET, so for the one parameter below
// INTERNAL == PLAIN. The slot is empty, not absent: when the first real
// control arrives from a DXi its ParamInfo::MapToInternal goes into
// toInternal() and the two ranges part company there and nowhere else.
//
// APPEND NEW PARAMETERS, NEVER INSERT. An id that moves loads a saved
// project's value into the wrong control.
//------------------------------------------------------------------------

#pragma once

#include "Project6Dsp.h"

#include "pluginterfaces/vst/vsttypes.h"

namespace Project6 {

//------------------------------------------------------------------------
enum Param : Steinberg::Vst::ParamID
{
	/** The placeholder. It is a real parameter with a real effect, not a
	    stub: it proves host -> parameter -> DSP end to end, and it is the
	    last stage of the signal path, which is where it will stay. */
	kOutputTrim,

	kNumParams
};

//------------------------------------------------------------------------
// WHERE A PER-BLOCK VALUE FROM THE DSP WOULD GO
//
// Not needed yet, and written down so the first person who needs one does
// not reach for a message instead. A message sent from process() is
// silently discarded by the host's connection proxy: it returns success
// and does nothing. data.outputParameterChanges is the mechanism that
// works - a hidden, read-only parameter appended AFTER the settings, with
// a kNumStoredParams boundary so the state code keeps saving only the
// settings. VocalFilter-VSTi's kLiveBase is the worked example.
//
// If you add one, remember the trap it cost there: a range check reading
// `id < kNumParams` is a bug waiting for the next append. Bound the
// published block by its own end, not by the end of the table.
//------------------------------------------------------------------------

/** Everything up to here is a SETTING and goes in the state stream.
    Identical to kNumParams while there are no published values. */
constexpr Steinberg::Vst::ParamID kNumStoredParams = kNumParams;

/** The VST3 bypass, which hosts expect. 1000 is the convention, and it is
    far past the end of kParams - so RANGE-CHECK every id before indexing
    the table. paramDef() below does. */
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
	    parameter in this blank project - it returns the plain value. */
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
extern const ParamDef kParams[kNumParams];

/** The plain value of a parameter, given the whole normalised set. */
inline double plainValue (const double* normalized, Steinberg::Vst::ParamID id)
{
	return kParams[id].toPlain (normalized[id]);
}

/** Look a definition up by id, RANGE-CHECKED. Returns kParams[kOutputTrim]
    for anything unknown - including kBypass, which is 1000 and is not in
    the table. */
const ParamDef& paramDef (Steinberg::Vst::ParamID id);

/** True for an id the table actually describes. */
inline bool isTableParam (Steinberg::Vst::ParamID id) { return id < kNumParams; }

//------------------------------------------------------------------------
} // namespace Project6
