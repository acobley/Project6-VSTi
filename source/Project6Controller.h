//------------------------------------------------------------------------
// Project6 - edit controller
//
// The parameter list the host sees, and the state the editor reads.
//------------------------------------------------------------------------

#pragma once

#include "Project6Params.h"
#include "Project6Sample.h"
#include "Project6Slots.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <string>
#include <vector>

namespace Project6 {

class Project6Editor;

//------------------------------------------------------------------------
class Project6Controller : public Steinberg::Vst::EditControllerEx1
{
public:
	Project6Controller () = default;
	~Project6Controller () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IEditController*)new Project6Controller;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setParamNormalized (
		Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	void editorAttached (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorRemoved (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorDestroyed (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;

	/** The rate the DSP is actually running at, so nothing drawn from a
	    filter shape can disagree with the filter it draws. 44100 until the
	    processor says otherwise. */
	double dspSampleRate () const { return mSampleRate; }

	/** True once the processor's published values have actually arrived.

	    Until then - and for ever, in a host that does not forward
	    data.outputParameterChanges - the panel shows what was ASKED for
	    instead of what is happening, which is the right pad lit early
	    rather than the wrong one for ever. */
	bool hasLiveValues () const { return mHaveLiveValues; }

	//--------------------------------------------------------------------
	// The sample slots
	//
	// The controller's copy is the one the PANEL draws. The processor's
	// is the one that gets saved. They are kept in step by the message
	// sent below and by the state stream both sides read - never by one
	// reaching into the other, which VST3 does not allow and which would
	// not survive the two being in different processes.
	//--------------------------------------------------------------------

	const SlotBank& slots () const { return mSlots; }

	/** How a slot's file actually read, as the processor reported it.

	    The panel needs this to know whether a slot can be clicked at all,
	    and to say WHY when it cannot. SampleStatus::Empty for a slot with
	    no file in it. */
	SampleStatus slotStatus (int index) const;

	/** How far through its file a slot is, 0 to 1 - what the pad draws as
	    a progress bar. Zero for a slot that is not sounding. */
	float slotProgress (int index) const;

	//--------------------------------------------------------------------
	// What was read out of a slot's file about its TEMPO.
	//
	// Reported by the processor on the status message, because that is
	// where it was discovered. The panel shows it in the pad's tooltip -
	// "90 BPM (inferred from length)" - so that a pad which sounds wrong
	// says why without anyone having to open the file.
	//--------------------------------------------------------------------

	/** The detected tempo, or 0 when none was found. */
	double slotTempo (int index) const;

	/** Where that came from. TempoSource::None when there is no tempo. */
	TempoSource slotTempoSource (int index) const;

	/** Whether the file declared itself a one-shot, which is why an
	    otherwise fitted pad is not being fitted. */
	bool slotOneShot (int index) const;

	/** The speed this pad's file is actually playing at, given its own
	    tempo, the project's, and the pad's mode.

	    Computed HERE from the shared fitSpeed(), not read back from the
	    processor: the two would then be two answers to one question, and
	    the tooltip could disagree with the sound. `projectBpm` is what
	    the panel knows of the host's tempo.

	    NOT const: getParamNormalized is not const in the SDK's
	    EditController, and reaching around that with a cast would be a
	    lie about which of the two objects owns the value. */
	double slotFitSpeed (int index, double projectBpm);

	/** Ask the processor for all sixty-four. Called from the editor's
	    timer, and only while an editor is open: nothing else wants these
	    and nobody should pay for them when the panel is shut. */
	void requestProgress ();

	/** A file was dropped on a slot, or a slot was emptied. Records it,
	    tells the processor, and refreshes every open editor.

	    THE ONE PLACE a slot's path changes. The editor calls this rather
	    than setting its own slot's text, exactly as a control's position
	    is only ever set from setParamNormalized. */
	void setSlotPath (int index, const std::string& path);

	/** A pad was dragged onto another pad.

	    `copy` false MOVES it, which is a SWAP: the two cells exchange, so
	    a mis-aimed drag can never destroy a loaded slot - the file that
	    was there comes back to where the drag started. `copy` true leaves
	    the source alone and overwrites the destination, because there is
	    nothing to swap a copy with.

	    THE WHOLE CELL TRAVELS, not just the file: the level, the launch
	    division and the tempo fit go with it, so a loop lands sounding
	    the way it did before it was moved. A pad is what you set up, and
	    dragging it should rearrange the bank rather than the filenames.

	    THE TRIGGERS DO NOT. Every pad whose file changes is STOPPED -
	    both of them on a move, the destination alone on a copy. A voice
	    left running through a buffer that has been swapped underneath it
	    is a click at best, and a pad playing a file nobody started it
	    with is the worst thing this panel could do.

	    Here rather than in the editor for the same reason setSlotPath is:
	    it is the one place a slot's contents change, and two open editors
	    must not each have their own idea of how. */
	void moveSlot (int from, int to, bool copy);

private:
	void addParameters ();
	void sendSlotToProcessor (int index, const std::string& path);

	/** One parameter, as a COMPLETE EDIT GESTURE - begin, set, perform,
	    end - so the host records it and every open editor's control
	    follows. Does nothing when the value is already what is asked for:
	    a move writes up to eight parameters and the ones that did not
	    change are automation points saying nothing. */
	void writeParam (Steinberg::Vst::ParamID tag, double normalized);

	/** The three settings that belong to a PAD rather than to a file, and
	    that travel with it when it is dragged. Named once, here, so
	    adding a fourth is one line rather than a hunt. */
	void exchangeSlotSettings (int from, int to, bool copy);

	/** Every open editor. A host may open more than one - two windows on
	    the same instance is legal - so this is a vector, not a pointer. */
	std::vector<Project6Editor*> mEditors;

	double mSampleRate = 44100.0;
	SlotBank mSlots;
	SampleStatus mSlotStatus[kSlotCount] = {};
	float mSlotProgress[kSlotCount] = {};
	double mSlotTempo[kSlotCount] = {};
	TempoSource mSlotTempoSource[kSlotCount] = {};
	bool mSlotOneShot[kSlotCount] = {};
	bool mHaveLiveValues = false;
};

//------------------------------------------------------------------------
} // namespace Project6
