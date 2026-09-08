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

/** Controller -> processor, from a drop on a sample slot: which slot, and
    the path of the file now in it.

    THIS DIRECTION IS THE UNUSUAL ONE, and it is legitimate for the same
    reason the message above is - the controller lives on the UI thread.
    It is how a file path reaches the half of the plug-in that saves the
    project, a path being nothing a parameter could carry.

    The path is sent as BINARY rather than as a string attribute: it is
    UTF-8 bytes with an explicit length, so nothing has to convert to
    UTF-16 and back, and a path that is not valid UTF-16 cannot be
    mangled on the way. An ABSENT path attribute means the slot was
    cleared.

    The limitation to know about: if a host never connects the two
    components, this message is never delivered and the drop reaches the
    panel but not the saved project. That is true of every VST3 message
    and is why parameters, which cannot be lost this way, carry
    everything that can be expressed as a number. */
static const char* const kProject6SlotMessage       = "Project6Slot";
static const char* const kProject6SlotIndexAttribute = "Slot";
static const char* const kProject6SlotPathAttribute  = "Path";

//------------------------------------------------------------------------
} // namespace Project6
