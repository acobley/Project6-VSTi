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

private:
	void addParameters ();
	void sendSlotToProcessor (int index, const std::string& path);

	/** Every open editor. A host may open more than one - two windows on
	    the same instance is legal - so this is a vector, not a pointer. */
	std::vector<Project6Editor*> mEditors;

	double mSampleRate = 44100.0;
	SlotBank mSlots;
	SampleStatus mSlotStatus[kSlotCount] = {};
	float mSlotProgress[kSlotCount] = {};
	bool mHaveLiveValues = false;
};

//------------------------------------------------------------------------
} // namespace Project6
