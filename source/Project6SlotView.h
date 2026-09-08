//------------------------------------------------------------------------
// Project6 - the sample slot view
//
// One cell of the 8 x 8 grid: a well you can drag a .wav onto from the
// Finder. Empty it is blank - just the well. Loaded it shows the file's
// name in white, shortened to fit.
//
// It is NOT in Project6Controls.*, and deliberately so. That file is the
// control set carried across SpyBand, VocalFilter and this plug-in with a
// namespace change and nothing else, so that a `diff` between the three
// copies shows only what genuinely differs; putting the first control
// that is unique to Project6 into it would end that. For the same reason
// this file does not call Project6Controls.cpp's file-local draw3dRect -
// a slot is a RECESSED well rather than a raised bar, so it wants the two
// edge colours the other way round, and it draws its own rather than
// exporting a helper out of a file that is meant to stay comparable.
//
// A SLOT IS NOT A PARAMETER. It carries no tag and never calls
// valueChanged, beginEdit or endEdit - a file path is not something a
// host can automate or interpolate - which is why it is a CView and not a
// CControl. What it does on a drop is call its handler; the handler
// writes through the controller, and the controller writes back to every
// open editor's slot. The view never sets its own text, exactly as the
// parameter controls never set their own value: ONE PLACE keeps the panel
// and the plug-in in step.
//------------------------------------------------------------------------

#pragma once

#include "Project6Slots.h"

#include "vstgui/vstgui.h"
#include "vstgui/lib/dragging.h"

#include <functional>
#include <string>

namespace Project6 {

//------------------------------------------------------------------------
class SpySampleSlot : public VSTGUI::CView, public VSTGUI::DropTargetAdapter
{
public:
	SpySampleSlot (const VSTGUI::CRect& size, int index);

	int index () const { return mIndex; }

	/** What this slot is holding. The empty string is an empty slot.
	    Called by the editor, never by this view itself. */
	void setPath (const std::string& path);
	const std::string& path () const { return mPath; }

	/** Called with (index, path) when a file the plug-in will take is
	    dropped here. */
	void setHandler (std::function<void (int, const std::string&)> handler);

	void draw (VSTGUI::CDrawContext* context) override;

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

	CLASS_METHODS (SpySampleSlot, VSTGUI::CView)

private:
	/** The first path in the package this plug-in will take, or "".

	    THE FIRST, not all of them. A drag of five files onto one slot is
	    a drop on ONE slot; spreading the other four across slots the user
	    did not point at is a surprise, and an expensive one when it
	    overwrites four that were already loaded. */
	static std::string firstAcceptedPath (VSTGUI::IDataPackage* package);

	/** Draw the name into `band`, in white, shortened until it fits:
	    first by shedding the .wav - every file here has one, so it is the
	    four characters least worth reading - then by dropping a font
	    size, and only then by cutting characters off the end. */
	void drawName (VSTGUI::CDrawContext* context, const VSTGUI::CRect& band);

	int mIndex = 0;
	std::string mPath;
	std::function<void (int, const std::string&)> mHandler;
	bool mDragOver = false;
};

//------------------------------------------------------------------------
} // namespace Project6
