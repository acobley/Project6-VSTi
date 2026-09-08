//------------------------------------------------------------------------
// Project6 - the sample slot view
//
// One cell of the 8 x 8 grid. Drag a .wav onto it from the Finder and it
// takes the file; click it and the file loops; click it again and the
// loop stops. Empty, it is blank - just the well.
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

	bool playing () const { return getValueNormalized () >= 0.5f; }

	int mIndex = 0;
	std::string mPath;
	SampleStatus mStatus = SampleStatus::Empty;
	std::function<void (int, const std::string&)> mHandler;
	bool mDragOver = false;
};

//------------------------------------------------------------------------
} // namespace Project6
