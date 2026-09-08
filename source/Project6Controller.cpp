//------------------------------------------------------------------------
// Project6 - edit controller implementation
//------------------------------------------------------------------------

#include "Project6Controller.h"
#include "Project6Editor.h"
#include "Project6IDs.h"
#include "Project6SlotState.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Project6 {

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Controller::initialize (FUnknown* context)
{
	const tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
		return result;

	addParameters ();
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Controller::terminate ()
{
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
void Project6Controller::addParameters ()
{
	for (ParamID id = 0; id < kNumParams; ++id)
	{
		// paramDef, not kParams[id]: the sixty-four slot triggers share
		// one definition and are not rows in the table.
		const ParamDef& def = paramDef (id);

		// NEVER PASS A NULL TITLE OR UNITS TO RangeParameter. It
		// dereferences both without a null check, and the symptom is the
		// VALIDATOR SEGFAULTING in the post-build step rather than
		// anything that points at this line. A parameter whose value
		// string carries its own unit wants an EMPTY units field, not a
		// null one, or the host renders "200 ms %". paramTitle is
		// documented never to return null; the guard costs nothing and
		// the failure it prevents costs an afternoon.
		const char* name = paramTitle (id);

		String128 title;
		String128 units;
		UString (title, str16BufferSize (String128)).assign (name ? name : "Parameter");
		UString (units, str16BufferSize (String128)).assign (def.units ? def.units : "");

		// The published values are READ-ONLY and HIDDEN: no host lists
		// them, nothing outside the plug-in can write them, and they exist
		// only as the route data.outputParameterChanges travels on.
		// kIsHidden implies kIsReadOnly and the absence of kCanAutomate,
		// but saying both is clearer than relying on that.
		const int32 flags = isLiveParam (id)
			? (ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden)
			: ParameterInfo::kCanAutomate;

		// A two-state or enumerated parameter wants a
		// StringListParameter, not a RangeParameter with a step count:
		// the host shows the names, and getParamStringByValue /
		// getParamValueByString round-trip through them exactly rather
		// than through a number that has to be re-derived. The slot
		// triggers read "Stopped" and "Playing" in a host's own list,
		// which is worth having sixty-four times over.
		if (def.type == ParamType::Bool || def.type == ParamType::Enum)
		{
			auto* list = new StringListParameter (title, id, units, flags);

			const int choices = (def.type == ParamType::Bool)
			                        ? 2
			                        : static_cast<int> (def.plainMax) + 1;
			for (int choice = 0; choice < choices; ++choice)
			{
				String128 choiceName;
				UString (choiceName, str16BufferSize (String128))
					.assign (paramChoiceName (id, choice));
				list->appendString (choiceName);
			}

			parameters.addParameter (list);
			list->setNormalized (def.defaultNormalized ());
			continue;
		}

		RangeParameter* parameter = new RangeParameter (
			title, id, units,
			def.plainMin, def.plainMax, def.plainDefault,
			def.stepCount, flags);

		// The validator round-trips every parameter through
		// getParamStringByValue / getParamValueByString AT ITS CURRENT
		// VALUE and warns above 1e-4. A LINEAR dB range round-trips
		// exactly, which is why the trim is linear in decibels. A
		// non-linear display mapping needs toString AND fromString
		// overridden, and must be exact at the default even if it rounds
		// elsewhere - the base RangeParameter reads a leading number as a
		// plain value, so a typed "200 ms" becomes 200 and clamps.
		parameters.addParameter (parameter);
	}

	// The host's own bypass. Its id is 1000, far past the end of the
	// table, which is why everything that indexes kParams range-checks
	// first.
	{
		String128 title;
		String128 units;
		UString (title, str16BufferSize (String128)).assign ("Bypass");
		UString (units, str16BufferSize (String128)).assign ("");

		parameters.addParameter (new StringListParameter (
			title, kBypass, units,
			ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass));

		if (auto* bypass = static_cast<StringListParameter*> (parameters.getParameter (kBypass)))
		{
			bypass->appendString (STR16 ("Off"));
			bypass->appendString (STR16 ("On"));
			bypass->setNormalized (0.0);
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Controller::setComponentState (IBStream* state)
{
	// THE SAME LAYOUT Project6Processor::getState writes. If one side
	// changes, both change - and a short stream resets everything it does
	// not mention back to its default, exactly as the processor does.
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return kResultFalse;

	// EVERY parameter back to its default first, not just the saved ones.
	// The sixty-four slot triggers live past kNumStoredParams and are
	// deliberately not in the stream, so this loop is the only thing that
	// stops a project reopening with whatever pads the previous one left
	// playing.
	for (ParamID id = 0; id < kNumParams; ++id)
		setParamNormalized (id, paramDef (id).defaultNormalized ());
	setParamNormalized (kBypass, 0.0);

	double value = 0.0;
	for (int32 i = 0; i < count; ++i)
	{
		if (!streamer.readDouble (value))
			break;
		if (i < static_cast<int32> (kNumStoredParams))
			setParamNormalized (static_cast<ParamID> (i),
			                    std::min (1.0, std::max (0.0, value)));
	}

	int32 bypass = 0;
	if (streamer.readInt32 (bypass))
		setParamNormalized (kBypass, bypass ? 1.0 : 0.0);

	// THE IDENTICAL BLOCK the processor writes, read by the identical
	// function - which is the whole reason writeSlots and readSlots are
	// in a file of their own rather than being this loop typed twice.
	// readSlots empties the bank first, so a project saved before the
	// slots existed loads with all sixty-four empty.
	readSlots (streamer, mSlots);

	// The processor is reading the files right now and will report each
	// one; until it does, a restored slot is assumed good for the same
	// reason a dropped one is.
	for (int index = 0; index < kSlotCount; ++index)
		mSlotStatus[index] = mSlots.loaded (index) ? SampleStatus::Loaded
		                                           : SampleStatus::Empty;

	// Usually there is no editor yet at this point - the host sets state
	// before opening a window - but "usually" is not "never", and a panel
	// showing the previous project's samples is a bad way to find out.
	for (auto* editor : mEditors)
		editor->refreshSlots ();

	return kResultOk;
}

//------------------------------------------------------------------------
SampleStatus Project6Controller::slotStatus (int index) const
{
	if (!isSlotIndex (index))
		return SampleStatus::Empty;

	return mSlotStatus[index];
}

//------------------------------------------------------------------------
void Project6Controller::setSlotPath (int index, const std::string& path)
{
	// REFUSED, not clamped, by SlotBank itself. A drop that somehow
	// carried a bad index must not land on a real slot.
	if (!mSlots.setPath (index, path))
		return;

	// Optimistic, and corrected by the processor's reply within one
	// message round trip. The alternative - leaving the previous file's
	// status in place - would show a red "not a WAV file" against a file
	// that has only just arrived and about which nothing is yet known,
	// which is a lie about a different file.
	mSlotStatus[index] = path.empty () ? SampleStatus::Empty : SampleStatus::Loaded;

	sendSlotToProcessor (index, path);

	// Every open editor, not just the one that was dropped on: a host may
	// have two windows on this instance, and the slot has to fill in both.
	for (auto* editor : mEditors)
		editor->refreshSlots ();
}

//------------------------------------------------------------------------
void Project6Controller::sendSlotToProcessor (int index, const std::string& path)
{
	// The controller lives on the UI thread, so a message is legitimate
	// here - the rule that bans them is about process(), not about
	// direction. See the note in Project6IDs.h for what happens in a host
	// that never connects the two components.
	auto* message = allocateMessage ();
	if (message == nullptr)
		return;

	FReleaser releaser (message);
	message->setMessageID (kProject6SlotMessage);
	message->getAttributes ()->setInt (kProject6SlotIndexAttribute, index);

	// An empty path sends NO path attribute at all, which is how the
	// processor recognises a cleared slot. setBinary with a zero size is
	// not portable across hosts and would be a second way of saying the
	// same thing.
	if (!path.empty ())
		message->getAttributes ()->setBinary (kProject6SlotPathAttribute,
		                                      path.data (),
		                                      static_cast<uint32> (path.size ()));

	sendMessage (message);
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Controller::notify (IMessage* message)
{
	if (message == nullptr)
		return kInvalidArgument;

	if (FIDStringsEqual (message->getMessageID (), kProject6SlotStatusMessage))
	{
		int64 index = -1;
		int64 status = 0;
		if (message->getAttributes ()->getInt (kProject6SlotIndexAttribute, index) == kResultOk
		    && message->getAttributes ()->getInt (kProject6SlotStatusAttribute, status)
		           == kResultOk
		    && isSlotIndex (static_cast<int> (index)))
		{
			mSlotStatus[index] = static_cast<SampleStatus> (status);

			for (auto* editor : mEditors)
				editor->refreshSlots ();
		}
		return kResultOk;
	}

	if (FIDStringsEqual (message->getMessageID (), kProject6SampleRateMessage))
	{
		double rate = 0.0;
		if (message->getAttributes ()->getFloat (kProject6SampleRateAttribute, rate)
		        == kResultOk && rate > 0.0)
		{
			mSampleRate = rate;
		}
		return kResultOk;
	}

	return EditControllerEx1::notify (message);
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API Project6Controller::createView (FIDString name)
{
	if (name && FIDStringsEqual (name, ViewType::kEditor))
		return new Project6Editor (this);
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Controller::setParamNormalized (ParamID tag, ParamValue value)
{
	const tresult result = EditControllerEx1::setParamNormalized (tag, value);
	if (result != kResultOk)
		return result;

	// A published value arriving is the proof that this host forwards
	// data.outputParameterChanges at all. Until one does, the panel shows
	// what was asked for rather than what is happening.
	if (isLiveParam (tag))
		mHaveLiveValues = true;

	// The host, an automation lane and the panel all arrive here, so this
	// is the ONE place a control's position is kept in step with the
	// parameter behind it.
	for (auto* editor : mEditors)
		editor->updateControl (tag, value);

	return result;
}

//------------------------------------------------------------------------
void Project6Controller::editorAttached (EditorView* editor)
{
	if (auto* e = dynamic_cast<Project6Editor*> (editor))
		if (std::find (mEditors.begin (), mEditors.end (), e) == mEditors.end ())
			mEditors.push_back (e);
}

//------------------------------------------------------------------------
void Project6Controller::editorRemoved (EditorView* editor)
{
	editorDestroyed (editor);
}

//------------------------------------------------------------------------
void Project6Controller::editorDestroyed (EditorView* editor)
{
	// DO NOT dynamic_cast HERE. EditorView::~EditorView() is one of the two
	// callers, and by then the Project6Editor sub-object is gone, so the
	// cast yields null, the entry survives as a DANGLING POINTER, and the
	// next setParamNormalized above walks it. Comparing UPCAST POINTERS is
	// well defined at every point in the destruction sequence.
	mEditors.erase (std::remove_if (mEditors.begin (), mEditors.end (),
	                                [editor] (Project6Editor* e) {
		                                return static_cast<EditorView*> (e) == editor;
	                                }),
	                mEditors.end ());
}

//------------------------------------------------------------------------
} // namespace Project6
