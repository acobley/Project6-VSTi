//------------------------------------------------------------------------
// Project6 - the grid's own controls
//
// Three of them: SpySampleSlot, one cell of the 8 x 8 bank; SpySlotLevel,
// the level bar under it; and SpyColumnButton, the box above each column
// that launches all of it. They are together because they are views of
// the same grid and none means anything without the others.
//
// One cell of the 8 x 8 grid. Drag a .wav onto it from the Finder and it
// takes the file; click it and the file loops from the next bar line;
// click it again and it stops at the one after that. Empty, it is blank -
// just the well.
//
// THE CLICK AND THE SOUND ARE NOW TWO DIFFERENT THINGS, and the slot has
// to show both. What was asked for is the trigger parameter - this
// control's own value. What is happening is published back by the
// processor and arrives through setSounding(). While they disagree the
// slot is WAITING FOR A BAR LINE, in whichever direction, and says so
// with an amber edge; a pad clicked into silence with no visible change
// would be the worst kind of control this panel could have.
//
// IT IS HALF A PARAMETER AND HALF NOT, and the split is worth knowing:
//
//   * the FILE PATH is not a parameter. No host can automate a string or
//     interpolate between two of them, so it travels to the processor as
//     a message and is saved in the state stream by hand;
//
//   * the PLAY TRIGGER is. It is a two-state number, and this project's
//     rule is that anything expressible as a number is a parameter -
//     because a message can be lost when a host does not connect the two
//     components and a parameter cannot. So the view is a CControl,
//     tagged with slotPlayParam(index), and a click is an ordinary edit
//     gesture that a host can record, undo and automate.
//
// The consequence is that this view sets its own VALUE on a click - as
// every CControl must, and as SpyToggle does - but never its own TEXT.
// The path arrives through the controller, which is the one place a
// slot's file changes and the reason two open editors cannot drift.
//
// It is NOT in Project6Controls.*, deliberately. That file is the control
// set carried across SpyBand, VocalFilter and this plug-in with a
// namespace change and nothing else, so that a `diff` between the three
// copies shows only what genuinely differs; putting the first control
// that is unique to Project6 into it would end that. For the same reason
// this file does not call Project6Controls.cpp's file-local draw3dRect -
// a slot is a RECESSED well rather than a raised bar, so it wants the two
// edge colours the other way round, and it draws its own rather than
// exporting a helper out of a file that is meant to stay comparable.
//------------------------------------------------------------------------

#pragma once

#include "Project6Sample.h"
#include "Project6Slots.h"

#include "vstgui/vstgui.h"
#include "vstgui/lib/dragging.h"

#include <functional>
#include <string>

namespace Project6 {

//------------------------------------------------------------------------
class SpySampleSlot : public VSTGUI::CControl, public VSTGUI::DropTargetAdapter
{
public:
	SpySampleSlot (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	               int32_t tag, int index);

	int index () const { return mIndex; }

	/** What this slot is holding. The empty string is an empty slot.
	    Called by the editor, never by this view itself. */
	void setPath (const std::string& path);
	const std::string& path () const { return mPath; }

	/** Show this INSTEAD of the filename, until it is set back to empty.

	    It exists for the level bar underneath, which is nine pixels tall
	    and has nowhere to print a number. While the bar is being dragged
	    the pad above it reads out the level in decibels and then goes
	    back to being a filename - the same trick VocalFilter's SpySlider
	    plays with its own value text, using space that is already there
	    rather than adding a readout to sixty-four cells. */
	void setOverlay (const std::string& text);

	/** How that file actually read, as the processor reported it. A slot
	    that will not play has to be able to SAY so - a slot that takes
	    the drop, shows the name and does nothing when clicked is the
	    worst thing a control can do. */
	void setStatus (SampleStatus status);
	SampleStatus status () const { return mStatus; }

	/** True when there is a loaded file here and a click should start it. */
	bool playable () const
	{
		return !mPath.empty () && mStatus == SampleStatus::Loaded;
	}

	/** Whether this slot is ACTUALLY making a sound, as published by the
	    processor. Not the same as its parameter: between a click and the
	    next bar line the two deliberately disagree. */
	void setSounding (bool sounding);
	bool sounding () const { return mSounding; }

	/** What the user has asked for - this control's own parameter. */
	bool armed () const { return getValueNormalized () >= 0.5f; }

	/** Armed and not yet sounding, or sounding and no longer armed:
	    either way, waiting for a bar line. Public because the column
	    button above this slot summarises it. */
	bool pending () const { return armed () != mSounding; }

	/** Called with (index, path) when a file the plug-in will take is
	    dropped here. */
	void setHandler (std::function<void (int, const std::string&)> handler);

	void draw (VSTGUI::CDrawContext* context) override;

	/** A click toggles the loop. Firing on DOWN rather than up, as
	    SpyToggle does: a pad should sound the instant it is hit. */
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;

	/** Deliberately inert. CControl's wheel handling would move the
	    trigger by fractions of its one step, which on a two-state
	    parameter means a pad that starts and stops as the pointer passes
	    over it. */
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	//--------------------------------------------------------------------
	// Drag and drop
	//
	// The workflow VSTGUI documents: getDropTarget() is asked for a target
	// when a drag enters the view, then onDragEnter / onDragMove / and
	// either onDragLeave or onDrop. Returning `this` is the SDK's own
	// pattern for a view that is its own target - see CTabButton and
	// ColorView - and is safe here because IDropTarget and CView share one
	// virtual IReference base.
	//--------------------------------------------------------------------
	VSTGUI::SharedPointer<VSTGUI::IDropTarget> getDropTarget () override { return this; }

	VSTGUI::DragOperation onDragEnter (VSTGUI::DragEventData data) override;
	VSTGUI::DragOperation onDragMove (VSTGUI::DragEventData data) override;
	void onDragLeave (VSTGUI::DragEventData data) override;
	bool onDrop (VSTGUI::DragEventData data) override;

	CLASS_METHODS (SpySampleSlot, VSTGUI::CControl)

private:
	/** The first path in the package this plug-in will take, or "".

	    THE FIRST, not all of them. A drag of five files onto one slot is
	    a drop on ONE slot; spreading the other four across slots the user
	    did not point at is a surprise, and an expensive one when it
	    overwrites four that were already loaded. */
	static std::string firstAcceptedPath (VSTGUI::IDataPackage* package);

	/** Draw the name into `band`, shortened until it fits: first by
	    shedding the .wav - every file here has one, so it is the four
	    characters least worth reading - then by dropping a font size, and
	    only then by cutting characters off the end. */
	void drawName (VSTGUI::CDrawContext* context, const VSTGUI::CRect& band);

	/** Path and, when there is something to explain, why it will not
	    play. */
	void refreshTooltip ();


	int mIndex = 0;
	std::string mPath;
	std::string mOverlay;
	SampleStatus mStatus = SampleStatus::Empty;
	bool mSounding = false;
	std::function<void (int, const std::string&)> mHandler;
	bool mDragOver = false;
};

//------------------------------------------------------------------------
/** The level bar under one pad.

    A SlideSpin's bar with everything else taken off: no label, no value
    text, no lamp - nine pixels of the DXi's own Draw3dRect and fill, and
    the same drag law, which is RELATIVE and one unit of a hundred per
    pixel. Absolute positioning on a bar this size would make every
    setting a coarse one, and it is what the original did not do either.

    It is a proper CControl with its own parameter tag, so a host can
    automate a slot's level exactly as it can automate its trigger. While
    it is dragged it asks the pad above to show the value, because there
    is no room for a number here. */
class SpySlotLevel : public VSTGUI::CControl
{
public:
	SpySlotLevel (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	              int32_t tag, int index);

	int index () const { return mIndex; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlotLevel, VSTGUI::CControl)

private:
	int mIndex = 0;
	bool mDragging = false;
	VSTGUI::CPoint mLastPoint;
};

//------------------------------------------------------------------------
/** The box above a column: one press launches every loaded slot in it.

    It writes the slots' own trigger parameters and does nothing else, so
    the bar-line rules apply to a column press exactly as they apply to a
    click on one pad - eight pads come in together on the next bar line,
    which is the only way eight loops can start in time with each other.

    IT CARRIES NO PARAMETER OF ITS OWN, and that is the design rather than
    an omission. A column "state" would be a second opinion about the same
    eight triggers, and the moment somebody clicked one pad out of a
    launched column the two would disagree with nothing to say which was
    right. The precedent is VocalFilter's SpyPresetButton, which writes
    nine parameters and holds none: a host sees the writes and never sees
    the button.

    So it is a CView, not a CControl, and what it SHOWS is a summary the
    editor hands it - how many of its column can play, how many are
    sounding, and whether any of them is waiting for a bar line. */
class SpyColumnButton : public VSTGUI::CView
{
public:
	SpyColumnButton (const VSTGUI::CRect& size, int column);

	int column () const { return mColumn; }

	void setHandler (std::function<void (int)> handler);

	/** The state of the column below, recomputed by the editor whenever
	    anything in it moves. */
	void setState (int playable, int sounding, bool pending);

	void draw (VSTGUI::CDrawContext* context) override;

	/** Fires on mouse DOWN, like the pads - and for the same reason the
	    pads can: nothing happens until the next bar line, so the wait IS
	    the undo. A press you did not mean can be pressed again before it
	    takes effect. */
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;

	CLASS_METHODS (SpyColumnButton, VSTGUI::CView)

private:
	int mColumn = 0;
	int mPlayable = 0;
	int mSounding = 0;
	bool mPending = false;
	std::function<void (int)> mHandler;
};

//------------------------------------------------------------------------
} // namespace Project6
