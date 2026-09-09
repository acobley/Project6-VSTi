//------------------------------------------------------------------------
// Project6 - editor
//
// There is no .rc behind this panel and no artwork to recover, so unlike
// the SpyBand and ForTran editors nothing here is converted from dialog
// units: the layout is in pixels, computed from one grid, and the grid is
// the only thing to change if the panel is resized.
//
// Two halves. Along the top, the plug-in's one parameter and the display;
// below it, the 8 x 8 bank of sample slots.
//
// THE SLOT GRID SETS THE PANEL'S WIDTH. Eight slots wide enough to read a
// filename in is 707 pixels, and everything above them is measured from
// that rather than the other way round - which is why the display's width
// is DERIVED and not a constant. Change kSlotWidth and the whole panel
// follows.
//
// The editor exists at all - rather than being left until later, which is
// what the blank-plug-in skill recommends - because the control set was
// being lifted from VocalFilter anyway. Everything in here that was
// hard-won there is preserved: the showValue discipline, the complete
// edit gesture in setParameter, and the timer that PULLS from the
// controller rather than being pushed by a control.
//------------------------------------------------------------------------

#pragma once

#include "Project6Controls.h"
#include "Project6Display.h"
#include "Project6Params.h"
#include "Project6SlotView.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <map>

namespace Project6 {

class Project6Controller;

//------------------------------------------------------------------------
class Project6Editor : public Steinberg::Vst::VSTGUIEditor,
                       public VSTGUI::IControlListener
{
public:
	explicit Project6Editor (Project6Controller* controller);

	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	// IControlListener
	void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;
	void controlBeginEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;
	void controlEndEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;

	/** The controller's setParamNormalized reaches the panel through
	    here. */
	void updateControl (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue normalized);

	//--------------------------------------------------------------------
	// The layout. Every position on the panel is derived from these, so
	// there are no magic numbers below and resizing means editing here.
	//--------------------------------------------------------------------
	static constexpr int kMargin       = 14;
	static constexpr int kSliderWidth  = 96;
	static constexpr int kSliderHeight = 27;
	static constexpr int kColumnGap    = 16;
	static constexpr int kRowGap       = 6;

	/** One column, one row of PARAMETER controls - for now. Adding one
	    means raising these and adding a row to the table in open(), not
	    moving anything. */
	static constexpr int kColumns = 1;
	static constexpr int kRows    = 1;

	static constexpr int kContentWidth =
		kColumns * kSliderWidth + (kColumns - 1) * kColumnGap;

	//--------------------------------------------------------------------
	// The slot grid, which sets the width of everything above it.
	//
	// A pad needs to be worth reading: at the panel's 8-point Arial, 84
	// pixels held about fourteen characters before SpySampleSlot started
	// shedding the extension, and below about 70 the grid stopped being a
	// list of names and became a grid of ellipses.
	//
	// It is 96 now, and the twelve pixels went to the strip beneath
	// rather than to the name: the strip has to carry a level bar, the
	// launch box AND the tempo-fit box, and a level bar narrower than
	// about thirty pixels is a bar you cannot set. Widening the pad is
	// what pays for the third box, and the name got wider as well.
	//--------------------------------------------------------------------
	static constexpr int kSlotWidth  = 96;
	static constexpr int kSlotHeight = 30;
	static constexpr int kSlotGap    = 5;

	/** The strip under each pad: its level bar, the box that says which
	    grid line it launches on, and the box that says what it does about
	    the project's tempo. Thirteen pixels, which is what the nine-point
	    face needs to put "1/4" in a box - the bar itself would have been
	    happy with nine, and a strip too short to letter would have meant
	    a second row.

	    The bar has no room for a number even at thirteen, which is why
	    the pad above shows the level in decibels while it is dragged.

	    THREE THINGS IN ONE STRIP rather than two strips: a second row
	    under every pad would cost sixteen pixels of height per row and
	    make the grid taller than a laptop screen, and the two boxes are
	    both small enumerated settings that are read rather than
	    manipulated. The bar keeps what is left, and kLevelWidth is
	    DERIVED so that the static_assert below catches a strip that no
	    longer adds up. */
	static constexpr int kStripHeight   = 13;
	static constexpr int kLevelGap      = 2;
	static constexpr int kDivisionWidth = 26;
	static constexpr int kFitWidth      = 26;
	static constexpr int kStripGap      = 3;
	static constexpr int kLevelWidth    =
		kSlotWidth - kDivisionWidth - kFitWidth - 2 * kStripGap;

	/** A CELL is a pad and the strip under it. The grid's row pitch is
	    this plus the gap between cells, so the strip belongs visually to
	    the pad above it rather than floating between two. */
	static constexpr int kCellHeight = kSlotHeight + kLevelGap + kStripHeight;

	static constexpr int kSlotGridWidth =
		kSlotColumns * kSlotWidth + (kSlotColumns - 1) * kSlotGap;
	static constexpr int kSlotGridHeight =
		kSlotRows * kCellHeight + (kSlotRows - 1) * kSlotGap;

	static constexpr int kTitleTop    = 4;
	static constexpr int kTitleHeight = 15;

	static constexpr int kHeadingTop    = kTitleTop + kTitleHeight + 8;
	static constexpr int kHeadingHeight = 14;

	static constexpr int kGridTop = kHeadingTop + kHeadingHeight + 3;

	//--------------------------------------------------------------------
	// The row faders, down the right of the grid.
	//
	// One per row, on the SUM of that row's eight pads - so they sit
	// beside the rows they control and the eye follows a row across into
	// its own fader. They are ordinary SpySliders, the same control the
	// output trim uses, because that is what they are: a labelled level
	// with a decibel readout.
	//--------------------------------------------------------------------
	static constexpr int kRowFaderGap   = 16;
	static constexpr int kRowFaderWidth = kSliderWidth;
	static constexpr int kRowFaderLeft  = kMargin + kSlotGridWidth + kRowFaderGap;

	/** The panel is eight slots, the fader column, and two margins. */
	static constexpr int kEditorWidth =
		kRowFaderLeft + kRowFaderWidth + kMargin;

	/** The display, to the RIGHT of the parameter controls. Its width is
	    DERIVED - whatever is left between the controls and the right
	    margin - so widening the slot grid widens the display rather than
	    leaving a strip of bare panel beside it. */
	static constexpr int kDisplayGap    = 16;
	static constexpr int kDisplayHeight = 132;
	static constexpr int kDisplayLeft   = kMargin + kContentWidth + kDisplayGap;
	static constexpr int kDisplayWidth  = kEditorWidth - kDisplayLeft - kMargin;
	static constexpr int kDisplayTop    = kHeadingTop;
	static constexpr int kDisplayBottom = kDisplayTop + kDisplayHeight;

	static constexpr int kSlotHeadingTop = kDisplayBottom + 16;

	/** A launch box above each column, the column's own width, with a gap
	    before the pads so it reads as heading a column rather than as
	    being the first cell of one. */
	static constexpr int kColumnButtonTop    = kSlotHeadingTop + kHeadingHeight + 3;
	static constexpr int kColumnButtonHeight = 18;
	static constexpr int kColumnButtonGap    = 5;

	static constexpr int kSlotGridTop =
		kColumnButtonTop + kColumnButtonHeight + kColumnButtonGap;

	static constexpr int kEditorHeight = kSlotGridTop + kSlotGridHeight + kMargin;

	static_assert (kDisplayWidth > 0, "the slot grid must be wider than the controls");

	// Every position on the panel is derived, so a change to kSlotWidth or
	// kSlotGap that leaves a ragged edge or a row hanging off the bottom
	// is arithmetic rather than something to notice in a screenshot.
	static_assert (kMargin + (kSlotColumns - 1) * (kSlotWidth + kSlotGap) + kSlotWidth
	                   == kMargin + kSlotGridWidth,
	               "the slot grid is not its own width");
	static_assert (kRowFaderLeft + kRowFaderWidth == kEditorWidth - kMargin,
	               "the row faders do not meet the right margin");
	static_assert (kSlotGridTop + (kSlotRows - 1) * (kCellHeight + kSlotGap) + kCellHeight
	                   == kEditorHeight - kMargin,
	               "the slot grid does not meet the bottom margin");

	// A launch box has to sit exactly over the column it launches, or the
	// panel is lying about which one it is.
	static_assert (kColumnButtonTop + kColumnButtonHeight < kSlotGridTop,
	               "the column buttons overlap the pads");

	// The strip under a pad is exactly as wide as the pad: a bar, a gap,
	// the division box, a gap, and the fit box.
	static_assert (kLevelWidth + kStripGap + kDivisionWidth + kStripGap + kFitWidth
	                   == kSlotWidth,
	               "the level bar and the two boxes do not fill the cell's width");
	static_assert (kLevelWidth >= 30,
	               "the boxes have eaten the level bar - a bar this narrow cannot be set");

	/** How often the panel asks the controller where the DSP is. 30 ms is
	    about 33 fps - fast enough that a smoothed move is a movement
	    rather than three steps, and slow enough to cost nothing. */
	static constexpr int kTimerMs = 30;

	/** A slot's path changed - because a file was dropped here, or in
	    another editor, or because a project was loaded. Refreshes the
	    whole bank from the controller; sixty-four string compares is
	    nothing beside a redraw, and SpySampleSlot::setPath invalidates
	    only the ones that actually moved. */
	void refreshSlots ();

	/** The playhead of every pad has arrived from the processor. */
	void refreshProgress ();

	/** What the panel knows of the host's tempo, from the published
	    kLiveTempo. Zero when the host has not said. */
	double projectTempo () const;

private:
	VSTGUI::CRect cell (int column, int row) const;

	/** One cell of the slot grid, from kSlotWidth and friends. */
	VSTGUI::CRect slotCell (int column, int row) const;

	/** The launch box above one column, exactly as wide as it. */
	VSTGUI::CRect columnCell (int column) const;

	/** The level bar under one pad, directly below it and taking what the
	    division box leaves. */
	VSTGUI::CRect levelCell (int column, int row) const;

	/** The launch-division box, beside that bar. */
	VSTGUI::CRect divisionCell (int column, int row) const;

	/** The tempo-fit box, at the right-hand end of the strip. */
	VSTGUI::CRect fitCell (int column, int row) const;

	/** One row's fader, beside its row and vertically centred on the
	    cell so it lines up with the pad rather than with the level bar
	    beneath it. */
	VSTGUI::CRect rowFaderCell (int row) const;

	/** Put a slot's level, in decibels, on the pad above its bar - and
	    take it off again. There is no room for a number on a nine-pixel
	    bar, and adding a readout to sixty-four cells would cost more
	    space than it gave. */
	void showLevelOverlay (Steinberg::Vst::ParamID tag, double normalized);
	void clearLevelOverlay (Steinberg::Vst::ParamID tag);

	/** A column's launch box was pressed: arm every loaded slot in it, or
	    stop them all if they are all already armed.

	    It writes the SLOTS' OWN TRIGGERS and nothing else, so a column
	    press and eight separate clicks are the same thing to the
	    processor - and the bar-line rules apply to both without knowing
	    the button exists. */
	void columnClicked (int column);

	/** Recompute what each column button shows from the slots below it. */
	void refreshColumns ();

	/** Work out whether one pad's tempo fit is actually doing anything,
	    and tell its box - so a mode that is set but idle says so. */
	void refreshFit (int index);

	/** A file was dropped on a slot. Writes through the CONTROLLER, which
	    tells the processor and calls refreshSlots on every open editor -
	    so the panel never sets its own slot text, exactly as it never
	    sets its own parameter values. */
	void slotDropped (int index, const std::string& path);

	/** A pad was dragged onto another pad. Passed straight to the
	    controller, which owns what a move actually means. */
	void slotMoved (int from, int to, bool copy);

	/** Write one parameter as a COMPLETE EDIT GESTURE - begin, set,
	    perform, end - so the host records it, undo works, and every open
	    editor's control follows. */
	void setParameter (Steinberg::Vst::ParamID tag, double plainValue);

	/** The timer's work: refresh the display and ask for the playheads. */
	void onTimer ();

	/** The display's own refresh: hand it what the DSP is doing. */
	void refreshDisplay ();

	/** One parameter's value in its own plain unit. */
	double plainOf (Steinberg::Vst::ParamID tag) const;

	/** Set what a control SHOWS, and mark it for redraw.
	 *
	 *  THE ONLY PLACE THIS EDITOR CALLS setValueNormalized.
	 *  CControl::setValue assigns the value and nothing else - it does not
	 *  mark the view dirty - so a value set without the invalidate
	 *  repaints only when something else happens to dirty the same region.
	 *  In VocalFilter that presented as a switch which was correct under
	 *  the mouse, because the control's own handler invalidates, and
	 *  intermittent under automation, because nothing did.
	 *  tools/check-editor.py fails if another call site appears. */
	void showValue (VSTGUI::CControl* control, double normalized);

	/** A slider bound to a parameter, labelled, and formatting its own
	    readout from the SAME table the host reads. */
	SpySlider* addSlider (Steinberg::Vst::ParamID tag, const char* label,
	                      const VSTGUI::CRect& rect);

	VSTGUI::CTextLabel* addHeading (const char* text, const VSTGUI::CRect& rect);

	Project6Controller* mController = nullptr;

	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> mControls;
	SpyDisplay* mDisplay = nullptr;
	SpySampleSlot* mSlots[kSlotCount] = { nullptr };
	SpySlotLevel* mLevels[kSlotCount] = { nullptr };
	SpySlotFit* mFits[kSlotCount] = { nullptr };
	SpyColumnButton* mColumns[kSlotColumns] = { nullptr };

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;
};

//------------------------------------------------------------------------
} // namespace Project6
