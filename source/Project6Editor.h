//------------------------------------------------------------------------
// Project6 - editor
//
// There is no .rc behind this panel and no artwork to recover, so unlike
// the SpyBand and ForTran editors nothing here is converted from dialog
// units: the layout is in pixels, computed from one grid, and the grid is
// the only thing to change if the panel is resized.
//
// ONE CONTROL, and the frame around it. The grid is a column-and-row
// arrangement rather than a single position because that is what every
// control added after this one will want, and retrofitting a grid to a
// panel laid out by hand is how magic numbers get in.
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

	/** One column, one row - for now. Adding a control means raising one
	    of these and adding a row to the table in open(), not moving
	    anything. */
	static constexpr int kColumns = 1;
	static constexpr int kRows    = 1;

	static constexpr int kContentWidth =
		kColumns * kSliderWidth + (kColumns - 1) * kColumnGap;

	static constexpr int kTitleTop    = 4;
	static constexpr int kTitleHeight = 15;

	static constexpr int kHeadingTop    = kTitleTop + kTitleHeight + 8;
	static constexpr int kHeadingHeight = 14;

	static constexpr int kGridTop = kHeadingTop + kHeadingHeight + 3;

	/** The display, to the RIGHT of the controls, and taller than they
	    are - it sets the height of the panel, and the controls grow
	    downwards into the space beside it. */
	static constexpr int kDisplayGap    = 16;
	static constexpr int kDisplayWidth  = 300;
	static constexpr int kDisplayHeight = 132;
	static constexpr int kDisplayLeft   = kMargin + kContentWidth + kDisplayGap;
	static constexpr int kDisplayTop    = kHeadingTop;
	static constexpr int kDisplayBottom = kDisplayTop + kDisplayHeight;

	static constexpr int kEditorWidth  = kDisplayLeft + kDisplayWidth + kMargin;
	static constexpr int kEditorHeight = kDisplayBottom + kMargin;

	/** How often the panel asks the controller where the DSP is. 30 ms is
	    about 33 fps - fast enough that a smoothed move is a movement
	    rather than three steps, and slow enough to cost nothing. */
	static constexpr int kTimerMs = 30;

private:
	VSTGUI::CRect cell (int column, int row) const;

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

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;
};

//------------------------------------------------------------------------
} // namespace Project6
