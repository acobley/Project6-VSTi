//------------------------------------------------------------------------
// Project6 - the parameter table
//
// Every range and default here comes from a constant in Project6Dsp.h, so
// the DSP's idea of what a value may be and the host's idea cannot drift
// apart. The rows are written out rather than generated, so a title can be
// read straight against the id it belongs to.
//------------------------------------------------------------------------

#include "Project6Params.h"

namespace Project6 {

//------------------------------------------------------------------------
// id            title          units type              plainMin     plainMax     plainDefault     internalMin  internalMax  steps smoothed
//------------------------------------------------------------------------
const ParamDef kParams[kNumParams] =
{
	// internal == plain, deliberately - see the banner in the header.
	{ kOutputTrim, "Output Trim", "dB", ParamType::Float, kTrimMinDb,  kTrimMaxDb,  kTrimDefaultDb,  kTrimMinDb,  kTrimMaxDb,  0,    true },
};

//------------------------------------------------------------------------
// The table has to agree with itself. These cost nothing at run time and
// fire at compile time, which is the only place a typo in a table like
// this is cheap to find.
//------------------------------------------------------------------------
static_assert (kNumParams == 1, "one placeholder parameter; append, never insert");
static_assert (kNumStoredParams == kNumParams,
               "no published values yet - if you add some, this boundary moves");
static_assert (kBypass > kNumParams,
               "kBypass must be past the end of the table, so indexing it is caught");

//------------------------------------------------------------------------
const ParamDef& paramDef (Steinberg::Vst::ParamID id)
{
	if (id < kNumParams)
		return kParams[id];
	return kParams[kOutputTrim];
}

//------------------------------------------------------------------------
} // namespace Project6
