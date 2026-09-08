//------------------------------------------------------------------------
// Project6 - edit controller implementation
//------------------------------------------------------------------------

#include "Project6Controller.h"
#include "Project6Editor.h"
#include "Project6IDs.h"

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
		const ParamDef& def = kParams[id];

		// NEVER PASS A NULL TITLE OR UNITS TO RangeParameter. It
		// dereferences both without a null check, and the symptom is the
		// VALIDATOR SEGFAULTING in the post-build step rather than
		// anything that points at this line. A parameter whose value
		// string carries its own unit wants an EMPTY units field, not a
		// null one, or the host renders "200 ms %".
		String128 title;
		String128 units;
		UString (title, str16BufferSize (String128)).assign (def.title ? def.title : "Parameter");
		UString (units, str16BufferSize (String128)).assign (def.units ? def.units : "");

		const int32 flags = ParameterInfo::kCanAutomate;

		// An enumerated parameter wants a StringListParameter, not a
		// RangeParameter with a step count: the host shows the names, and
		// getParamStringByValue / getParamValueByString round-trip through
		// them exactly. There are none yet; the branch is here so the
		// first one is added in the right place.
		if (def.type == ParamType::Enum)
			continue;

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

	for (ParamID id = 0; id < kNumStoredParams; ++id)
		setParamNormalized (id, kParams[id].defaultNormalized ());
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

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Project6Controller::notify (IMessage* message)
{
	if (message == nullptr)
		return kInvalidArgument;

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
