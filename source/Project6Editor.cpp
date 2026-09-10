//------------------------------------------------------------------------
// Project6 - editor implementation
//------------------------------------------------------------------------

#include "Project6Editor.h"
#include "Project6Controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace Project6 {

namespace {

/** The panel behind the controls. SpyBand's dialog background was a
    bitmap; this one is a colour, because there is no artwork to recover
    and inventing some would only be something else to keep in step with
    the layout. The value is SpyBand's own fallback - the colour its editor
    paints under the bitmap so a missing file reads as a dark panel rather
    than as whatever the host left in the window. */
const CColor kPanel (64, 64, 64, 255);

} // namespace

//------------------------------------------------------------------------
Project6Editor::Project6Editor (Project6Controller* controller)
: VSTGUIEditor (controller)
, mController (controller)
{
	ViewRect rect (0, 0, kEditorWidth, kEditorHeight);
	setRect (rect);
}

//------------------------------------------------------------------------
CRect Project6Editor::cell (int column, int row) const
{
	const CCoord x = kMargin + column * (kSliderWidth + kColumnGap);
	const CCoord y = kGridTop + row * (kSliderHeight + kRowGap);
	return CRect (x, y, x + kSliderWidth, y + kSliderHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::slotCell (int column, int row) const
{
	const CCoord x = kMargin + column * (kSlotWidth + kSlotGap);
	const CCoord y = kSlotGridTop + row * (kCellHeight + kSlotGap);
	return CRect (x, y, x + kSlotWidth, y + kSlotHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::levelCell (int column, int row) const
{
	const CRect pad = slotCell (column, row);
	return CRect (pad.left, pad.bottom + kLevelGap,
	              pad.left + kLevelWidth, pad.bottom + kLevelGap + kStripHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::divisionCell (int column, int row) const
{
	// In the MIDDLE of the strip now: the bar, this, then the fit box.
	const CRect pad = slotCell (column, row);
	const CCoord left = pad.left + kLevelWidth + kStripGap;
	return CRect (left, pad.bottom + kLevelGap,
	              left + kDivisionWidth, pad.bottom + kLevelGap + kStripHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::fitCell (int column, int row) const
{
	const CRect pad = slotCell (column, row);
	return CRect (pad.right - kFitWidth, pad.bottom + kLevelGap,
	              pad.right, pad.bottom + kLevelGap + kStripHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::columnCell (int column) const
{
	const CCoord x = kMargin + column * (kSlotWidth + kSlotGap);
	return CRect (x, kColumnButtonTop,
	              x + kSlotWidth, kColumnButtonTop + kColumnButtonHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::rowFaderCell (int row) const
{
	// The LABEL AND THE FADER TOGETHER are centred where the fader alone
	// used to be, so the pair still reads as belonging to the row beside
	// it rather than sitting high in the cell.
	const CRect cell = slotCell (0, row);
	const CCoord stack = kRowChannelHeight + kRowChannelGap + kSliderHeight;
	const CCoord top = cell.top + (kCellHeight - stack) * 0.5
	                   + kRowChannelHeight + kRowChannelGap;
	return CRect (kRowFaderLeft, top,
	              kRowFaderLeft + kRowFaderWidth, top + kSliderHeight);
}

//------------------------------------------------------------------------
CRect Project6Editor::rowChannelCell (int row) const
{
	// DERIVED FROM THE FADER, not from the cell a second time: the two
	// cannot drift apart, and moving one moves the other.
	const CRect fader = rowFaderCell (row);
	return CRect (fader.left, fader.top - kRowChannelGap - kRowChannelHeight,
	              fader.right, fader.top - kRowChannelGap);
}

//------------------------------------------------------------------------
void Project6Editor::setParameter (ParamID tag, double plain)
{
	if (mController == nullptr)
		return;

	const ParamDef& def = paramDef (tag);
	const double normalized = std::min (1.0, std::max (0.0, def.toNormalized (plain)));

	// A COMPLETE GESTURE. beginEdit / endEdit around it is what makes a
	// host treat a write as something it can record and undo, rather than
	// as an unexplained jump; setParamNormalized is what moves this
	// panel's own control, because it comes back through updateControl.
	mController->beginEdit (tag);
	mController->setParamNormalized (tag, normalized);
	mController->performEdit (tag, normalized);
	mController->endEdit (tag);
}

//------------------------------------------------------------------------
SpySlider* Project6Editor::addSlider (ParamID tag, const char* label, const CRect& rect)
{
	auto* control = new SpySlider (rect, this, static_cast<int32_t> (tag));
	control->setLabel (label);

	// The number across the middle is the parameter's own PLAIN value,
	// read out of the same table the host formats from - so the panel and
	// the host cannot disagree about what a control says. Hertz get no
	// decimal place; decibels and per cent get one, because a dB is worth
	// resolving and 100 % should not read as 99 %.
	const ParamDef& def = paramDef (tag);
	const bool integral = (def.units != nullptr && def.units[0] == 'H');
	control->setFormatter ([def, integral] (float normalized)
	{
		char buffer[32];
		std::snprintf (buffer, sizeof (buffer), integral ? "%.0f %s" : "%.1f %s",
		               def.toPlain (normalized), def.units ? def.units : "");
		return std::string (buffer);
	});

	mControls[tag] = control;
	if (mController)
		showValue (control, mController->getParamNormalized (tag));

	// Z-order is the order views are added, and every control here is a
	// direct child of the frame - so a control's getViewSize() is already
	// in frame coordinates and nothing needs a parent chain walked.
	frame->addView (control);
	return control;
}

//------------------------------------------------------------------------
CTextLabel* Project6Editor::addHeading (const char* text, const CRect& rect, CFontRef font)
{
	auto* label = new CTextLabel (rect);
	label->setFont (font != nullptr ? font : panelFont ());
	label->setFontColor (Colours::kLabel);
	label->setBackColor (kTransparentCColor);
	label->setFrameColor (kTransparentCColor);
	label->setStyle (CParamDisplay::kNoFrame);
	label->setText (text);
	frame->addView (label);
	return label;
}

//------------------------------------------------------------------------
bool PLUGIN_API Project6Editor::open (void* parent, const PlatformType& platformType)
{
	if (frame != nullptr)
		return false;

	const CRect frameSize (0, 0, kEditorWidth, kEditorHeight);
	frame = new CFrame (frameSize, this);
	frame->setBackgroundColor (kPanel);

	//--------------------------------------------------------------------
	// The title.
	//--------------------------------------------------------------------
	addHeading ("Project6  -  instrument",
	            CRect (kMargin, kTitleTop, kEditorWidth - kMargin,
	                   kTitleTop + kTitleHeight));

	//--------------------------------------------------------------------
	// The heading over the control column.
	//--------------------------------------------------------------------
	{
		const CRect head = cell (0, 0);
		addHeading ("Output",
		            CRect (head.left, kHeadingTop, head.right,
		                   kHeadingTop + kHeadingHeight));
	}

	//--------------------------------------------------------------------
	// The controls. A TABLE, not a sequence of hand-placed calls: adding
	// the next one is a row here and a bump to kRows / kColumns in the
	// header, and nothing else on the panel moves.
	//--------------------------------------------------------------------
	{
		static const struct { ParamID tag; const char* label; int column, row; } kGrid[] =
		{
			{ kOutputTrim, "Output Trim", 0, 0 },
		};

		for (const auto& entry : kGrid)
			addSlider (entry.tag, entry.label, cell (entry.column, entry.row));
	}

	//--------------------------------------------------------------------
	// A fader per row, on the sum of that row's eight pads. Ordinary
	// sliders through the same addSlider the output trim uses, so they
	// label themselves, read out in decibels from the same table the host
	// formats from, and follow automation with nothing added.
	//--------------------------------------------------------------------
	for (int row = 0; row < kSlotRows; ++row)
	{
		static const char* const kRowLabels[kSlotRows] =
			{ "Row A", "Row B", "Row C", "Row D", "Row E", "Row F", "Row G", "Row H" };

		addSlider (rowLevelParam (row), kRowLabels[row], rowFaderCell (row));

		// WHICH MIDI CHANNEL THIS ROW PLAYS OUT ON, above the fader.
		//
		// A row is a BUS, and its channel and its level are two facts
		// about the same bus - so they belong together rather than the
		// channel being something to look up in the notes. It matters
		// most in a host that shows only the merged MIDI output, where
		// the channel IS the routing and there is nothing else on screen
		// to say which row is which.
		//
		// The number comes from midiChannelNumberForRow, which is derived
		// from the one the processor stamps on the events - see
		// Project6Midi.h for why that is two functions and not one.
		char channel[32] = {};
		std::snprintf (channel, sizeof (channel), "MIDI ch %d",
		               midiChannelNumberForRow (row));

		auto* label = addHeading (channel, rowChannelCell (row), panelFontTiny ());
		label->setHoriAlign (kCenterText);
	}

	//--------------------------------------------------------------------
	// The display, filling the space to the right.
	//--------------------------------------------------------------------
	mDisplay = new SpyDisplay (
		CRect (kDisplayLeft, kDisplayTop,
		       kDisplayLeft + kDisplayWidth, kDisplayBottom));
	frame->addView (mDisplay);

	//--------------------------------------------------------------------
	// The sample slots: eight by eight, in reading order, each one a drop
	// target for a .wav from the Finder.
	//
	// The handler captures `this` and an INDEX rather than a pointer to
	// the slot. The slot outlives nothing here, but an index cannot be
	// left dangling by a later refactor - and it is the index, not the
	// view, that the controller and the state stream speak in.
	//--------------------------------------------------------------------
	addHeading ("Samples  -  drag .wav files onto the slots; a click launches on the next bar",
	            CRect (kMargin, kSlotHeadingTop, kMargin + kSlotGridWidth,
	                   kSlotHeadingTop + kHeadingHeight));

	// The fader is on the way to the MIX. Each row also leaves on its own
	// output before the fader, which is worth saying on the panel because
	// nothing about a fader suggests that something bypasses it.
	addHeading ("Row levels  (direct outs tap before these)",
	            CRect (kRowFaderLeft - 130, kSlotHeadingTop, kEditorWidth - kMargin,
	                   kSlotHeadingTop + kHeadingHeight));

	// The column launch boxes, above the pads they launch. Added FIRST so
	// they sit under the slots in z-order, which matters not at all here
	// and would the moment anything overlapped.
	for (int column = 0; column < kSlotColumns; ++column)
	{
		auto* button = new SpyColumnButton (columnCell (column), column);
		button->setHandler ([this] (int at) { columnClicked (at); });
		mColumns[column] = button;
		frame->addView (button);
	}

	for (int row = 0; row < kSlotRows; ++row)
	{
		for (int column = 0; column < kSlotColumns; ++column)
		{
			const int index = slotIndex (column, row);

			// TAGGED WITH ITS PLAY PARAMETER, so a click is an ordinary
			// edit gesture and the slot goes into mControls with the
			// sliders. That is what makes updateControl move it when a
			// host automates the trigger: the pad follows the automation
			// lane through machinery that was already here.
			auto* slot = new SpySampleSlot (
				slotCell (column, row), this,
				static_cast<int32_t> (slotPlayParam (index)), index);

			slot->setHandler ([this] (int at, const std::string& path)
			                  { slotDropped (at, path); });

			// A pad dropped on another pad. Straight to the controller,
			// which is the one place a slot's contents change - the
			// editor has nothing to add and two open editors must not
			// each have their own idea of what a move is.
			slot->setMoveHandler ([this] (int from, int to, bool copy)
			                      { slotMoved (from, to, copy); });

			mControls[slotPlayParam (index)] = slot;
			if (mController)
				showValue (slot, mController->getParamNormalized (slotPlayParam (index)));

			mSlots[index] = slot;
			frame->addView (slot);

			// The level bar under it, tagged with its own parameter, so a
			// host can automate a slot's level exactly as it can automate
			// the trigger above it.
			auto* level = new SpySlotLevel (
				levelCell (column, row), this,
				static_cast<int32_t> (slotLevelParam (index)), index);

			mControls[slotLevelParam (index)] = level;
			if (mController)
				showValue (level, mController->getParamNormalized (slotLevelParam (index)));

			mLevels[index] = level;
			frame->addView (level);

			// And the box beside it, saying which grid line this pad
			// waits for. Its own parameter, so a host can automate a
			// pad's quantisation like everything else here.
			auto* division = new SpySlotDivision (
				divisionCell (column, row), this,
				static_cast<int32_t> (slotDivisionParam (index)), index);

			mControls[slotDivisionParam (index)] = division;
			if (mController)
				showValue (division,
				           mController->getParamNormalized (slotDivisionParam (index)));

			frame->addView (division);

			// And the box beside THAT, saying what this pad does when its
			// file's tempo is not the project's. Its own parameter for
			// the same reason again - it is a setting, and every setting
			// on this panel is automatable.
			auto* fit = new SpySlotFit (
				fitCell (column, row), this,
				static_cast<int32_t> (slotFitParam (index)), index);

			mControls[slotFitParam (index)] = fit;
			if (mController)
				showValue (fit, mController->getParamNormalized (slotFitParam (index)));

			mFits[index] = fit;
			frame->addView (fit);
		}
	}

	// The slots put their FULL PATH in a tooltip, because the name on the
	// slot is shortened and two takes of the same sample usually differ
	// only in the directory. Nothing else on this panel has one.
	frame->enableTooltips (true);

	refreshSlots ();
	refreshDisplay ();

	// The display follows the DSP, which can move without anything on the
	// panel being touched - a host automating the trim, or a smoother
	// still travelling - so it is PULLED on a timer rather than pushed by
	// a control.
	mTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { onTimer (); },
	                                  kTimerMs, true);

	frame->open (parent, platformType);
	return true;
}

//------------------------------------------------------------------------
void Project6Editor::onTimer ()
{
	refreshDisplay ();

	// The playheads. ASKED FOR, not pushed: the processor cannot send
	// from process(), and sixty-four continuously changing published
	// parameters would flood a host's queue to move bars that redraw
	// thirty times a second. See Project6IDs.h.
	//
	// Only while an editor is open, because this is the only thing that
	// asks - a shut panel costs nothing.
	if (mController)
		mController->requestProgress ();
}

//------------------------------------------------------------------------
void Project6Editor::refreshProgress ()
{
	if (frame == nullptr || mController == nullptr)
		return;

	for (int index = 0; index < kSlotCount; ++index)
		if (mSlots[index])
			mSlots[index]->setProgress (mController->slotProgress (index));
}

//------------------------------------------------------------------------
void Project6Editor::refreshDisplay ()
{
	if (mDisplay == nullptr || mController == nullptr)
		return;

	// The published transport values. toInternal already rounds an Int
	// parameter, so nothing here adds a half.
	const int transport = static_cast<int> (
		paramDef (kLiveTransport).toInternal (
			mController->getParamNormalized (kLiveTransport)));
	const int beats = static_cast<int> (
		paramDef (kLiveBeatsPerBar).toInternal (
			mController->getParamNormalized (kLiveBeatsPerBar)));

	mDisplay->setState (plainOf (kOutputTrim), mController->dspSampleRate (),
	                    transport, mController->getParamNormalized (kLiveBarPhase),
	                    beats);
}

//------------------------------------------------------------------------
void Project6Editor::slotDropped (int index, const std::string& path)
{
	if (mController == nullptr || ! isSlotIndex (index))
		return;

	// Straight to the controller. It records the path, tells the
	// processor, and calls refreshSlots on every editor it has open -
	// including this one, which is how this slot's text actually changes.
	mController->setSlotPath (index, path);
}

//------------------------------------------------------------------------
void Project6Editor::slotMoved (int from, int to, bool copy)
{
	if (mController == nullptr)
		return;

	// Everything - the swap, the settings that travel, the pads that get
	// stopped - is the controller's, for the same reason slotDropped's
	// work is. See Project6Controller::moveSlot.
	mController->moveSlot (from, to, copy);
}

//------------------------------------------------------------------------
void Project6Editor::columnClicked (int column)
{
	if (mController == nullptr || column < 0 || column >= kSlotColumns)
		return;

	// Only the slots that can actually play count. A column of eight with
	// two files in it is a column of two, and pressing it should not
	// pretend otherwise.
	int playable = 0;
	int armed = 0;
	for (int row = 0; row < kSlotRows; ++row)
	{
		const int index = slotIndex (column, row);
		if (mSlots[index] == nullptr || !mSlots[index]->playable ())
			continue;

		++playable;
		if (mController->getParamNormalized (slotPlayParam (index)) >= 0.5)
			++armed;
	}

	if (playable == 0)
		return;

	// The rule lives in Project6Slots.h, with a test, because the
	// half-armed case is a judgement rather than an obvious answer.
	const bool arm = columnClickArms (playable, armed);

	for (int row = 0; row < kSlotRows; ++row)
	{
		const int index = slotIndex (column, row);
		if (mSlots[index] == nullptr || !mSlots[index]->playable ())
			continue;

		// ONLY THE ONES THAT CHANGE. Writing a trigger that is already
		// where it should be would put a point in a host's automation
		// lane that says nothing, eight times per press.
		const bool already = mController->getParamNormalized (slotPlayParam (index)) >= 0.5;
		if (already == arm)
			continue;

		setParameter (slotPlayParam (index), arm ? 1.0 : 0.0);
	}
}

//------------------------------------------------------------------------
void Project6Editor::refreshColumns ()
{
	for (int column = 0; column < kSlotColumns; ++column)
	{
		if (mColumns[column] == nullptr)
			continue;

		int playable = 0;
		int sounding = 0;
		bool pending = false;

		for (int row = 0; row < kSlotRows; ++row)
		{
			const SpySampleSlot* slot = mSlots[slotIndex (column, row)];
			if (slot == nullptr || !slot->playable ())
				continue;

			++playable;
			if (slot->sounding ())
				++sounding;
			if (slot->pending ())
				pending = true;
		}

		mColumns[column]->setState (playable, sounding, pending);
	}
}

//------------------------------------------------------------------------
void Project6Editor::refreshSlots ()
{
	if (frame == nullptr || mController == nullptr)
		return;

	const SlotBank& bank = mController->slots ();
	for (int index = 0; index < kSlotCount; ++index)
	{
		if (mSlots[index] == nullptr)
			continue;

		mSlots[index]->setPath (bank.path (index));

		// The status too, and in that order: a slot draws its name in the
		// colour its status decides, so setting the path first and the
		// status second means at most one redraw shows the old verdict
		// about the new file, and setStatus invalidates again.
		mSlots[index]->setStatus (mController->slotStatus (index));

		// What it is ACTUALLY doing, if the processor has told us. If it
		// has not - a host that does not forward published values, or
		// simply the first moments after the editor opened - fall back to
		// what was asked for, so the pad lights early rather than never.
		mSlots[index]->setSounding (
			mController->getParamNormalized (
				mController->hasLiveValues () ? liveSlotParam (index)
				                              : slotPlayParam (index)) >= 0.5);

		// WHICH KIND OF PAD, so the well is drawn as what it holds and
		// the two controls that do nothing to notes say so.
		const SlotFileKind kind = mController->slotKind (index);
		mSlots[index]->setKind (kind);

		if (mLevels[index])
			mLevels[index]->setApplies (kind != SlotFileKind::Midi);

		refreshFit (index);
	}

	// The column boxes summarise the slots, so they follow every change
	// to one - a file arriving, a status coming back, a pad starting.
	refreshColumns ();
}

//------------------------------------------------------------------------
double Project6Editor::projectTempo () const
{
	// THE HOST'S TEMPO, as the panel knows it - which is only through the
	// published bar phase and beats per bar, neither of which is a tempo.
	//
	// So it comes from the controller's own copy, published for exactly
	// this: see kLiveTempo. Zero means the host has not said, and
	// fitSpeed turns that into no fit, which is the same answer the DSP
	// reaches from the same function.
	if (mController == nullptr)
		return 0.0;

	return paramDef (kLiveTempo).toInternal (
		mController->getParamNormalized (kLiveTempo));
}

//------------------------------------------------------------------------
void Project6Editor::refreshFit (int index)
{
	if (!isSlotIndex (index) || mFits[index] == nullptr || mController == nullptr)
		return;

	const double fileBpm = mController->slotTempo (index);
	const double speed   = mController->slotFitSpeed (index, projectTempo ());

	// FITTED means the pad's audio is actually being altered. A mode of
	// Off, an undetected tempo, a one-shot, or a file that is already at
	// the project's tempo all come out as a speed of exactly 1 - and in
	// every one of those cases the box has to say so rather than imply
	// that something is happening.
	mFits[index]->setFitted (std::fabs (speed - 1.0) > 1e-9);

	// And the tooltip says WHICH of those it was, because they need
	// different things done about them.
	std::string text;
	if (mController->slotStatus (index) == SampleStatus::Empty)
		text.clear ();
	else if (mController->slotKind (index) == SlotFileKind::Midi)
	{
		// A MIDI PAD IS ALWAYS AT THE PROJECT'S TEMPO, by construction:
		// its notes are placed in quarter notes, so there is nothing for
		// a fit to do and nothing a fit could improve. Saying that is
		// more use than leaving the box unexplained.
		text = "MIDI — always at the project's tempo";
	}
	else if (mController->slotOneShot (index))
		text = "one-shot — never fitted";
	else if (fileBpm <= 0.0)
		text = "no tempo found in this file";
	else
	{
		char line[128] = {};
		std::snprintf (line, sizeof (line), "%.1f BPM (%s), playing at %.3fx",
		               fileBpm, tempoSourceText (mController->slotTempoSource (index)),
		               speed);
		text = line;
	}

	mFits[index]->setTempoText (text);

	// The same line on the pad above, which is the bigger target - and on
	// a MIDI pad, what the pad says instead is what is actually in the
	// file and where it goes.
	if (mSlots[index])
		mSlots[index]->setTempoText (
			mController->slotKind (index) == SlotFileKind::Midi ? midiText (index) : text);
}

//------------------------------------------------------------------------
std::string Project6Editor::midiText (int index) const
{
	if (mController == nullptr || !isSlotIndex (index))
		return std::string ();

	const int notes = mController->slotNoteCount (index);
	const double beats = mController->slotBeats (index);
	if (notes <= 0)
		return std::string ();

	// THE ROUNDING IS DONE HERE, with the shared function, rather than
	// sent from the processor - it depends on the project's time
	// signature, which changes without any file being reloaded.
	const int numerator = static_cast<int> (
		paramDef (kLiveBeatsPerBar).toInternal (
			mController->getParamNormalized (kLiveBeatsPerBar)));

	// The DENOMINATOR is not published, and 4 is the assumption. It is
	// the right one almost always, and where it is wrong the tooltip
	// names a bar count that is out by a factor rather than a loop that
	// plays wrongly - the processor uses the host's real signature.
	const double bar = barQuartersFor (numerator, 4);
	const double loop = loopLengthQuarters (beats, bar);

	char line[192] = {};
	std::snprintf (line, sizeof (line),
	               "%d notes, %.2f beats — looping as %.0f bar%s on MIDI channel %d",
	               notes, beats, loop / bar, (loop / bar) > 1.5 ? "s" : "",
	               midiChannelNumberForRow (rowOfSlot (index)));
	return line;
}

//------------------------------------------------------------------------
void Project6Editor::showLevelOverlay (ParamID tag, double normalized)
{
	const int slot = slotOfLevelParam (tag);
	if (!isSlotIndex (slot) || mSlots[slot] == nullptr)
		return;

	// The parameter's own plain value, formatted from the same table the
	// host formats from - so the pad and the host cannot disagree about
	// what the bar is set to.
	char buffer[32];
	std::snprintf (buffer, sizeof (buffer), "%.1f dB",
	               slotLevelDef ().toPlain (normalized));

	mSlots[slot]->setOverlay (buffer);
}

//------------------------------------------------------------------------
void Project6Editor::clearLevelOverlay (ParamID tag)
{
	const int slot = slotOfLevelParam (tag);
	if (!isSlotIndex (slot) || mSlots[slot] == nullptr)
		return;

	mSlots[slot]->setOverlay (std::string ());
}

//------------------------------------------------------------------------
void Project6Editor::showValue (CControl* control, double normalized)
{
	if (control == nullptr)
		return;
	control->setValueNormalized (static_cast<float> (normalized));
	control->invalid ();
}

//------------------------------------------------------------------------
double Project6Editor::plainOf (ParamID tag) const
{
	if (mController == nullptr)
		return 0.0;
	return paramDef (tag).toPlain (mController->getParamNormalized (tag));
}

//------------------------------------------------------------------------
void PLUGIN_API Project6Editor::close ()
{
	mTimer = nullptr;

	mControls.clear ();
	mDisplay = nullptr;
	for (auto*& slot : mSlots)
		slot = nullptr;
	for (auto*& level : mLevels)
		level = nullptr;
	for (auto*& column : mColumns)
		column = nullptr;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
void Project6Editor::valueChanged (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	const ParamValue value = control->getValueNormalized ();

	mController->setParamNormalized (tag, value);
	mController->performEdit (tag, value);

	// While a level bar is moving, the pad above it reads out the value.
	if (isSlotLevelParam (tag))
		showLevelOverlay (tag, value);
}

//------------------------------------------------------------------------
void Project6Editor::controlBeginEdit (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	mController->beginEdit (tag);

	// The readout appears when the gesture starts, not on the first
	// pixel of movement: a bar pressed and not yet moved should still
	// say what it is set to.
	if (isSlotLevelParam (tag))
		showLevelOverlay (tag, control->getValueNormalized ());
}

//------------------------------------------------------------------------
void Project6Editor::controlEndEdit (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	mController->endEdit (tag);

	// And the filename comes back.
	if (isSlotLevelParam (tag))
		clearLevelOverlay (tag);
}

//------------------------------------------------------------------------
void Project6Editor::updateControl (ParamID tag, ParamValue normalized)
{
	if (frame == nullptr)
		return;

	//--------------------------------------------------------------------
	// The published values have no control of their own behind them: they
	// are what the processor says is happening, and the panel READS them.
	// They are handled first, and return, so nothing below has to know
	// they exist.
	//--------------------------------------------------------------------
	if (isLiveSlotParam (tag))
	{
		const int slot = slotOfLiveParam (tag);
		if (isSlotIndex (slot) && mSlots[slot])
		{
			mSlots[slot]->setSounding (normalized >= 0.5);
			refreshColumns ();
		}
		return;
	}

	if (tag == kLiveTransport || tag == kLiveBarPhase || tag == kLiveBeatsPerBar)
	{
		refreshDisplay ();
		return;
	}

	auto it = mControls.find (tag);
	if (it == mControls.end () || it->second == nullptr)
		return;

	showValue (it->second, normalized);

	// In a host that never forwards published values there is nothing to
	// tell a pad it has started, so a trigger has to light its own slot.
	// Where published values DO arrive this would be a lie for the length
	// of one bar, which is exactly the wait the panel is meant to show -
	// hence the guard.
	if (isSlotPlayParam (tag) && !mController->hasLiveValues ())
	{
		const int slot = slotOfPlayParam (tag);
		if (isSlotIndex (slot) && mSlots[slot])
			mSlots[slot]->setSounding (normalized >= 0.5);
	}

	// A trigger moving changes whether its column is waiting for a bar
	// line, whether or not anything is sounding yet.
	if (isSlotPlayParam (tag))
		refreshColumns ();

	// A FIT MODE MOVING CHANGES WHETHER IT IS DOING ANYTHING, and the box
	// draws that. Without this, switching a pad from off to varispeed
	// would put "spd" in the box and leave it dimmed - the panel saying
	// the setting is idle at the very moment it stopped being.
	if (isSlotFitParam (tag))
		refreshFit (slotOfFitParam (tag));

	// The tempo the panel fits against is one of the published values,
	// and a host that changes tempo mid-project changes what every pad is
	// doing. Cheaper to redo all sixty-four than to work out which.
	if (tag == kLiveTempo)
	{
		for (int slot = 0; slot < kSlotCount; ++slot)
			refreshFit (slot);
	}

	// The display reads parameters rather than controls, so it has to be
	// told too - the timer would get there within 30 ms, but a control
	// moved by the host should not visibly lag the panel it moved.
	refreshDisplay ();
}

//------------------------------------------------------------------------
} // namespace Project6
