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
	// 84 pixels is what a filename needs to be worth reading: at the
	// panel's 8-point Arial it holds about fourteen characters before
	// SpySampleSlot starts shedding the extension, which covers most of
	// how people actually name samples. Below about 70 the grid stops
	// being a list of names and becomes a grid of ellipses.
	//--------------------------------------------------------------------
	static constexpr int kSlotWidth  = 84;
	static constexpr int kSlotHeight = 30;
	static constexpr int kSlotGap    = 5;

	static constexpr int kSlotGridWidth =
		kSlotColumns * kSlotWidth + (kSlotColumns - 1) * kSlotGap;
	static constexpr int kSlotGridHeight =
		kSlotRows * kSlotHeight + (kSlotRows - 1) * kSlotGap;

	static constexpr int kTitleTop    = 4;
	static constexpr int kTitleHeight = 15;

	static constexpr int kHeadingTop    = kTitleTop + kTitleHeight + 8;
	static constexpr int kHeadingHeight = 14;

	static constexpr int kGridTop = kHeadingTop + kHeadingHeight + 3;

	/** The panel is exactly as wide as eight slots and two margins. */
	static constexpr int kEditorWidth = kMargin + kSlotGridWidth + kMargin;

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

	// The grid has to FILL its margins, at both ends. Every position on
	// the panel is derived, so a change to kSlotWidth or kSlotGap that
	// leaves a ragged right edge or a row hanging off the bottom is
	// arithmetic rather than something to notice in a screenshot.
	static_assert (kMargin + (kSlotColumns - 1) * (kSlotWidth + kSlotGap) + kSlotWidth
	                   == kEditorWidth - kMargin,
	               "the slot grid does not meet the right margin");
	static_assert (kSlotGridTop + (kSlotRows - 1) * (kSlotHeight + kSlotGap) + kSlotHeight
	                   == kEditorHeight - kMargin,
	               "the slot grid does not meet the bottom margin");

	// A launch box has to sit exactly over the column it launches, or the
	// panel is lying about which one it is.
	static_assert (kColumnButtonTop + kColumnButtonHeight < kSlotGridTop,
	               "the column buttons overlap the pads");

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

private:
	VSTGUI::CRect cell (int column, int row) const;

	/** One cell of the slot grid, from kSlotWidth and friends. */
	VSTGUI::CRect slotCell (int column, int row) const;

	/** The launch box above one column, exactly as wide as it. */
	VSTGUI::CRect columnCell (int column) const;

	/** A column's launch box was pressed: arm every loaded slot in it, or
	    stop them all if they are all already armed.

	    It writes the SLOTS' OWN TRIGGERS and nothing else, so a column
	    press and eight separate clicks are the same thing to the
	    processor - and the bar-line rules apply to both without knowing
	    the button exists. */
	void columnClicked (int column);

	/** Recompute what each column button shows from the slots below it. */
	void refreshColumns ();

	/** A file was dropped on a slot. Writes through the CONTROLLER, which
	    tells the processor and calls refreshSlots on every open editor -
	    so the panel never sets its own slot text, exactly as it never
	    sets its own parameter values. */
	void slotDropped (int index, const std::string& path);

	/** Write one parameter as a COMPLETE EDIT GESTURE - begin, set,
	    perform, end - so the host records it, undo works, and every open
	    editor's control follows. */
	void setParameter (Steinberg::Vst::ParamID tag, double plainValue);

	/** The timer's work: hand the display what the DSP is doing. */
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
	SpyColumnButton* mColumns[kSlotColumns] = { nullptr };

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;
};

//------------------------------------------------------------------------
} // namespace Project6
