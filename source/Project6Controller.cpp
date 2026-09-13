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
#include <cstring>

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
	// A project saved before a parameter existed carries a shorter stream,
	// and whatever it does not mention must go back to its default rather
	// than keeping what the previous patch left in this instance.
	//
	// That now includes the sixty-four triggers: they ARE in the stream
	// as of version 9, in a block of their own past kNumStoredParams, so
	// a version 8 project reopens with nothing armed because this loop
	// defaulted them and nothing read over it.
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

	// THE SAME SECOND BLOCK, through the same function. The levels are
	// parameters but not part of the contiguous run at the top of the
	// stream, so they arrive here rather than in the loop above.
	{
		double levels[kSlotCount] = {};
		readValueBlock (streamer, levels, kSlotCount,
		                slotLevelDef ().defaultNormalized ());
		for (int slot = 0; slot < kSlotCount; ++slot)
			setParamNormalized (slotLevelParam (slot), levels[slot]);

		double rows[kSlotRows] = {};
		readValueBlock (streamer, rows, kSlotRows, rowLevelDef ().defaultNormalized ());
		for (int row = 0; row < kSlotRows; ++row)
			setParamNormalized (rowLevelParam (row), rows[row]);

		double divisions[kSlotCount] = {};
		readValueBlock (streamer, divisions, kSlotCount,
		                slotDivisionDef ().defaultNormalized ());
		for (int slot = 0; slot < kSlotCount; ++slot)
			setParamNormalized (slotDivisionParam (slot), divisions[slot]);

		double fits[kSlotCount] = {};
		readValueBlock (streamer, fits, kSlotCount, slotFitDef ().defaultNormalized ());
		for (int slot = 0; slot < kSlotCount; ++slot)
			setParamNormalized (slotFitParam (slot), fits[slot]);

		double loops[kSlotCount] = {};
		readValueBlock (streamer, loops, kSlotCount, slotLoopDef ().defaultNormalized ());
		for (int slot = 0; slot < kSlotCount; ++slot)
			setParamNormalized (slotLoopParam (slot), loops[slot]);

		double transposes[kSlotCount] = {};
		readValueBlock (streamer, transposes, kSlotCount,
		                slotTransposeDef ().defaultNormalized ());
		for (int slot = 0; slot < kSlotCount; ++slot)
			setParamNormalized (slotTransposeParam (slot), transposes[slot]);

		// WHICH PADS ARE ARMED. The identical block the processor writes,
		// read by the identical function, in the identical order - which
		// is the only reason the two sides cannot drift.
		//
		// A pad whose file turns out not to load is un-armed when its
		// status comes back from the processor; see notify(). It cannot
		// be decided here, because this side does not read files.
		double triggers[kSlotCount] = {};
		readValueBlock (streamer, triggers, kSlotCount,
		                slotPlayDef ().defaultNormalized ());
		for (int slot = 0; slot < kSlotCount; ++slot)
			setParamNormalized (slotPlayParam (slot), triggers[slot]);
	}

	// The processor is reading the files right now and will report each
	// one; until it does, a restored slot is assumed good for the same
	// reason a dropped one is.
	for (int index = 0; index < kSlotCount; ++index)
	{
		mSlotStatus[index] = mSlots.loaded (index) ? SampleStatus::Loaded
		                                           : SampleStatus::Empty;

		// NO TEMPO UNTIL THE PROCESSOR REPORTS ONE. Assuming a file is
		// good is safe - the status message corrects it in a moment -
		// but assuming a TEMPO would put a number in the tooltip that
		// was read from some other file.
		mSlotTempo[index]       = 0.0;
		mSlotTempoSource[index] = TempoSource::None;
		mSlotOneShot[index]     = false;
		mSlotKind[index]        = slotFileKind (mSlots.path (index));
		mSlotNotes[index]       = 0;
		mSlotBeats[index]       = 0.0;
	}

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
float Project6Controller::slotProgress (int index) const
{
	if (!isSlotIndex (index))
		return 0.f;

	return mSlotProgress[index];
}

//------------------------------------------------------------------------
double Project6Controller::slotTempo (int index) const
{
	if (!isSlotIndex (index))
		return 0.0;

	return mSlotTempo[index];
}

//------------------------------------------------------------------------
TempoSource Project6Controller::slotTempoSource (int index) const
{
	if (!isSlotIndex (index))
		return TempoSource::None;

	return mSlotTempoSource[index];
}

//------------------------------------------------------------------------
bool Project6Controller::slotOneShot (int index) const
{
	if (!isSlotIndex (index))
		return false;

	return mSlotOneShot[index];
}

//------------------------------------------------------------------------
SlotFileKind Project6Controller::slotKind (int index) const
{
	if (!isSlotIndex (index))
		return SlotFileKind::None;

	return mSlotKind[index];
}

//------------------------------------------------------------------------
int Project6Controller::slotNoteCount (int index) const
{
	if (!isSlotIndex (index))
		return 0;

	return mSlotNotes[index];
}

//------------------------------------------------------------------------
double Project6Controller::slotBeats (int index) const
{
	if (!isSlotIndex (index))
		return 0.0;

	return mSlotBeats[index];
}

//------------------------------------------------------------------------
double Project6Controller::slotFitSpeed (int index, double projectBpm)
{
	if (!isSlotIndex (index))
		return 1.0;

	// THE SAME THREE RULES the DSP applies, through the same shared
	// function: the mode, the one-shot flag, and fitSpeed's own handling
	// of an unknown tempo at either end.
	const FitMode mode = fitModeFromIndex (static_cast<int> (
		slotFitDef ().toInternal (getParamNormalized (slotFitParam (index)))));

	// A MIDI PAD IS NEVER FITTED. Its notes are placed in quarter notes,
	// so it is already at the project's tempo by construction and there
	// is nothing for a fit to do - see Project6Midi.h.
	if (mode == FitMode::Off || mSlotOneShot[index]
	    || mSlotKind[index] == SlotFileKind::Midi)
		return 1.0;

	return fitSpeed (mSlotTempo[index], projectBpm);
}

//------------------------------------------------------------------------
void Project6Controller::requestProgress ()
{
	// The UI thread asking the UI thread. If the host has not connected
	// the two components there is no reply and the bars simply do not
	// move - the same limitation every message here has, and the least
	// costly place for it to land.
	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kProject6ProgressRequestMessage);
		sendMessage (message);
	}
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

	// The tempo, though, is NOT guessed at optimistically. The previous
	// file's tempo against the new file's name would be worse than no
	// tempo at all, so the tooltip says nothing until the processor has
	// actually read the file.
	mSlotTempo[index]       = 0.0;
	mSlotTempoSource[index] = TempoSource::None;
	mSlotOneShot[index]     = false;

	// The KIND, though, IS guessed from the extension straight away -
	// unlike the tempo, it is a fact about the path rather than about
	// the file's contents, so the panel can draw a MIDI pad as a MIDI
	// pad from the moment it is dropped. The processor's reply confirms
	// it, or corrects it to None if the file would not parse.
	mSlotKind[index]  = slotFileKind (path);
	mSlotNotes[index] = 0;
	mSlotBeats[index] = 0.0;

	sendSlotToProcessor (index, path);

	// Every open editor, not just the one that was dropped on: a host may
	// have two windows on this instance, and the slot has to fill in both.
	for (auto* editor : mEditors)
		editor->refreshSlots ();
}

//------------------------------------------------------------------------
void Project6Controller::writeParam (ParamID tag, double normalized)
{
	const double clamped = std::min (1.0, std::max (0.0, normalized));

	// ONLY WHAT CHANGED. Eight gestures per drag, most of them writing a
	// value back over itself, would fill a host's undo history and an
	// automation lane with points that say nothing.
	if (getParamNormalized (tag) == clamped)
		return;

	beginEdit (tag);
	setParamNormalized (tag, clamped);
	performEdit (tag, clamped);
	endEdit (tag);
}

//------------------------------------------------------------------------
void Project6Controller::exchangeSlotSettings (int from, int to, bool copy)
{
	// THE PAD'S OWN SETTINGS, as opposed to the file's: what it is set
	// to, when it launches, and what it does about the tempo. The file's
	// tempo is not here because it is read out of the file and belongs
	// to it, not to the cell.
	const ParamID settings[][2] = {
		{ slotLevelParam (from),    slotLevelParam (to) },
		{ slotDivisionParam (from), slotDivisionParam (to) },
		{ slotFitParam (from),      slotFitParam (to) },
	};

	for (const auto& pair : settings)
	{
		// READ BOTH BEFORE WRITING EITHER. A swap that wrote the first
		// value across and then read the second would read the one it had
		// just written, and both pads would end up the same.
		const double source = getParamNormalized (pair[0]);
		const double target = getParamNormalized (pair[1]);

		writeParam (pair[1], source);
		if (! copy)
			writeParam (pair[0], target);
	}
}

//------------------------------------------------------------------------
void Project6Controller::moveSlot (int from, int to, bool copy)
{
	if (!isSlotIndex (from) || !isSlotIndex (to) || from == to)
		return;

	// NOTHING TO MOVE. An empty pad cannot be picked up in the first
	// place - the view refuses - so this is belt and braces against a
	// drag whose source was emptied while it was in flight.
	const std::string fromPath = mSlots.path (from);
	if (fromPath.empty ())
		return;

	const std::string toPath = mSlots.path (to);

	// STOPPED FIRST, before the file under it changes. The destination
	// always; the source only when it is losing its file, which is to say
	// on a move and not on a copy.
	writeParam (slotPlayParam (to), 0.0);
	if (! copy)
		writeParam (slotPlayParam (from), 0.0);

	// THE SETTINGS BEFORE THE PATHS, so that the path change is the LAST
	// thing to happen and the refreshSlots it triggers sees a cell that
	// has already finished moving. The other order leaves each pad's fit
	// box drawn from the settings it had a moment ago until the next
	// message arrives to correct it.
	exchangeSlotSettings (from, to, copy);

	// The paths, through the one door: setSlotPath tells the processor,
	// refreshes every open editor, and clears the tempo the panel was
	// showing until the processor reports the new file's own.
	setSlotPath (to, fromPath);
	if (! copy)
		setSlotPath (from, toPath);        // the SWAP - may be empty, which empties it
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

	if (FIDStringsEqual (message->getMessageID (), kProject6ProgressDataMessage))
	{
		const void* data = nullptr;
		uint32 size = 0;
		if (message->getAttributes ()->getBinary (kProject6ProgressAttribute, data, size)
		        == kResultOk
		    && data != nullptr && size == sizeof (mSlotProgress))
		{
			// The size check is the whole validation: a build with a
			// different slot count would send a different number of
			// floats, and reading it as if it were ours would be reading
			// somebody else's memory.
			std::memcpy (mSlotProgress, data, sizeof (mSlotProgress));

			for (auto* editor : mEditors)
				editor->refreshProgress ();
		}
		return kResultOk;
	}

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

			// AN ARMED PAD WITH NOTHING TO PLAY IS NOT ARMED. The
			// processor reached the same conclusion from the same fact
			// and has already cleared its own copy; this is the panel
			// side of it, so a restored pad whose file has gone does not
			// sit lit and waiting for a sound that cannot arrive.
			if (mSlotStatus[index] != SampleStatus::Loaded)
				setParamNormalized (slotPlayParam (static_cast<int> (index)), 0.0);

			// The tempo rides along. Absent attributes leave the defaults
			// in place rather than failing the whole message: an older
			// processor - or a build mid-upgrade - should still get its
			// status through.
			double tempo = 0.0;
			int64  source = static_cast<int64> (TempoSource::None);
			int64  oneShot = 0;
			message->getAttributes ()->getFloat (kProject6SlotTempoAttribute, tempo);
			message->getAttributes ()->getInt (kProject6SlotTempoSrcAttribute, source);
			message->getAttributes ()->getInt (kProject6SlotOneShotAttribute, oneShot);

			mSlotTempo[index]       = (tempo > 0.0) ? tempo : 0.0;
			mSlotTempoSource[index] = static_cast<TempoSource> (source);
			mSlotOneShot[index]     = (oneShot != 0);

			int64 kind = static_cast<int64> (SlotFileKind::None);
			int64 notes = 0;
			double beats = 0.0;
			message->getAttributes ()->getInt (kProject6SlotKindAttribute, kind);
			message->getAttributes ()->getInt (kProject6SlotNotesAttribute, notes);
			message->getAttributes ()->getFloat (kProject6SlotBeatsAttribute, beats);

			mSlotKind[index]  = static_cast<SlotFileKind> (kind);
			mSlotNotes[index] = static_cast<int> (notes);
			mSlotBeats[index] = beats;

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
