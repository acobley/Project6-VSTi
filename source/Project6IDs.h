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

/** Processor -> controller: how a slot's file actually read.

    The reply to the message above, and sent again for every slot from
    setActive - which is the ONLY thing that makes a panel opened later,
    or a project loaded before the components were connected, show the
    truth rather than an optimistic guess.

    It exists because a slot that will not play has to be able to SAY so.
    A slot that takes the drop, shows the name and does nothing when
    clicked is the failure this project keeps coming back to: a control
    that looks live and is not. The value is a SampleStatus. */
static const char* const kProject6SlotStatusMessage    = "Project6SlotStatus";
static const char* const kProject6SlotStatusAttribute  = "Status";

/** What the reader made of the file's TEMPO, carried on that same
    message rather than on one of its own.

    The tempo is discovered at exactly the moment the status is - both
    come out of the same parseWav call - and it is needed at exactly the
    same moment, by the same tooltip. A second message would be a second
    thing to keep in step, a second thing to re-send from setActive, and
    a second chance for the panel to show a tempo for a file that failed
    to load.

    Tempo is a double and travels as a float attribute; the source is a
    TempoSource and the one-shot flag a bool, and both travel as ints
    because that is what the attribute list offers. */
static const char* const kProject6SlotTempoAttribute   = "Tempo";
static const char* const kProject6SlotTempoSrcAttribute = "TempoSource";
static const char* const kProject6SlotOneShotAttribute = "OneShot";

/** And what KIND of file it turned out to be, with the two numbers that
    only mean anything for a MIDI one: how many notes are in it and how
    long it is in quarter notes BEFORE the rounding up to a bar.

    The rounding itself is not sent. It depends on the project's time
    signature, which changes without any file being reloaded, so the
    panel does it with loopLengthQuarters exactly as the processor does -
    the shared function rather than a number that could go stale. */
static const char* const kProject6SlotKindAttribute    = "Kind";
static const char* const kProject6SlotNotesAttribute   = "Notes";
static const char* const kProject6SlotBeatsAttribute   = "Beats";

/** The playhead of every slot, for the progress bars on the pads.

    A REQUEST AND A REPLY, both on the UI thread: the controller asks on
    the editor's timer and the processor answers with all sixty-four at
    once. That is ForTran's scope idiom, and it is used here for the same
    two reasons.

    The first is the rule at the top of this file - a message sent from
    process() is silently discarded, so the processor cannot simply push
    this. The second is that the OTHER mechanism, publishing through
    data.outputParameterChanges, is wrong for this particular value:
    sixty-four continuously changing parameters would put thousands of
    points a second into a host's queue to move bars that redraw at
    thirty frames. A value that only the panel wants, only while it is
    open, and only at the rate it can draw, is a value to ASK for.

    The reply is one binary blob of kSlotCount floats, 0 to 1 each. */
static const char* const kProject6ProgressRequestMessage = "Project6ProgressRequest";
static const char* const kProject6ProgressDataMessage    = "Project6ProgressData";
static const char* const kProject6ProgressAttribute      = "Progress";

//------------------------------------------------------------------------
} // namespace Project6
