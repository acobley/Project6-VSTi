//------------------------------------------------------------------------
// Project6 - class UIDs and message identifiers
//
// These UIDs were generated fresh for this plug-in from os.urandom. NEVER
// CHANGE THEM once a build has been shipped: hosts store them in the
// project file, so a changed UID means every existing session silently
// loses the plug-in.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Project6 {

static const Steinberg::FUID kProject6ProcessorUID  (0x5719455B, 0x0531CEF2, 0xEF57C7B2, 0xA1E72CD8);
static const Steinberg::FUID kProject6ControllerUID (0x063D7358, 0x66DE8BEF, 0x5B3C7E20, 0x11A89909);

// The plug-in category is declared once, in Project6Entry.cpp, using the
// SDK's own PlugType::kInstrumentSynth - not repeated as a string here.

//------------------------------------------------------------------------
// Processor <-> controller messages
//
// RESERVED SPACE, and the rule that governs it. Every message must travel
// on the UI THREAD. sendMessage from process() returns success and is then
// silently discarded by the host's connection proxy - it does nothing and
// tells you nothing. Anything the DSP produces per block goes out through
// data.outputParameterChanges instead, as a hidden read-only parameter;
// see the note beside kBypass in Project6Params.h.
//
// Messages are legitimate from setActive, setState and notify, which is
// where the one below is sent from. Add new ones here, with a comment
// saying which of those they are sent from.
//------------------------------------------------------------------------

/** Processor -> controller, from setActive: the sample rate the DSP is
    actually running at.

    The panel needs it because anything drawn from a filter shape is a
    function of f/fs, so a display that assumed 44.1 k while the DSP ran at
    96 k would be drawing something nobody is hearing. There is no filter
    here yet - the display uses the rate only to report it - but the route
    is in place so the first one does not have to invent it. */
static const char* const kProject6SampleRateMessage   = "Project6SampleRate";
static const char* const kProject6SampleRateAttribute = "SampleRate";

//------------------------------------------------------------------------
} // namespace Project6
