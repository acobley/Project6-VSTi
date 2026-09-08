//------------------------------------------------------------------------
// Project6 - custom VSTGUI controls
//
// LIFTED from ~/DXi-DEv/VocalFilter-VSTi/source/VocalFilterControls.*,
// which was itself lifted from SpyBand-VSTi/source/SpyBandControls.*. The
// only change on this hop is the NAMESPACE; not one line of drawing or
// mouse handling differs from VocalFilter's copy.
//
// THE CLASS NAMES ARE DELIBERATELY UNCHANGED. SpySlider is still
// SpySlider, so `diff` against either of the other two copies shows only
// what genuinely differs, and a fix made in any of them can be carried to
// the others by hand. Renaming them would buy tidiness and cost that.
//
// Two things to know before editing this file:
//
//   * kValue IS NOT THE DXi's RED. The original is (192, 50, 50) and
//     SpyBand still uses it; VocalFilter changed the readouts to
//     near-white so they would not compete with a display's saturated
//     traces, and that choice is carried here. IT IS THE ONE COLOUR THAT
//     DIFFERS between the SpyBand copy and this one - keep it in mind
//     when diffing them.
//
//   * NONE OF THESE CONTROLS USES A BITMAP. The DXi property page they
//     descend from drew everything with GDI rectangles and text over the
//     panel, which is the whole reason they port at all, and why this
//     file needs no artwork in resource/. Keep any new control the same
//     way and the panel stays resolution-independent for free.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/vstgui.h"

#include <functional>
#include <string>
#include <vector>

namespace Project6 {

//------------------------------------------------------------------------
// The original's palette
//------------------------------------------------------------------------
namespace Colours {

const VSTGUI::CColor kBarLight   (200, 200, 200, 255);  // Draw3dRect top-left
const VSTGUI::CColor kBarHigh    (255, 255, 255, 255);  // Draw3dRect bottom-right
const VSTGUI::CColor kBarFill    (100, 100, 100, 255);
const VSTGUI::CColor kLabel      ( 50, 255,  50, 255);  // SetTextColor, green
const VSTGUI::CColor kValue      (232, 232, 232, 255);  // DrawTheText - see the banner
const VSTGUI::CColor kLampOn     (255,   0,   0, 255);
const VSTGUI::CColor kLampOff    (  0,   0,   0, 255);
const VSTGUI::CColor kLampFrame  (100, 100, 100, 255);
const VSTGUI::CColor kGrid       (200, 200, 200, 255);
const VSTGUI::CColor kGridBorder (100, 255, 100, 255);
const VSTGUI::CColor kOuterBorder(100, 100, 100, 255);
const VSTGUI::CColor kPin        (255,   0,   0, 255);
const VSTGUI::CColor kTrace      (127, 200, 255, 255);  // DrawArea's polyline

} // namespace Colours

/** MFC's CreatePointFont(80) - Arial at 8 points. */
VSTGUI::CFontRef panelFont ();

/** The same face one and two sizes down, for a label too long for its
    control. Six of the DXi's labels are wider than the control they name -
    "Unvoiced Noise Level" wants 95 pixels and has 82 - and DT_WORDBREAK
    wrapped them into an 11-pixel band, which clipped the second line.
    Dropping a size instead is what these are for. */
VSTGUI::CFontRef panelFontSmall ();
VSTGUI::CFontRef panelFontTiny ();

//------------------------------------------------------------------------
/** A SlideSpin in its ordinary mode: drag left and right.

    The DXi moved the value by ONE unit of a 0..100 range per pixel of
    horizontal movement, in either direction, with no absolute
    positioning - clicking did not jump the value to the pointer. That is
    preserved, because on a control 69 pixels wide an absolute drag would
    make every setting a coarse one. */
class SpySlider : public VSTGUI::CControl
{
public:
	SpySlider (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	/** The green text under the bar - the DXi's SetLabel. */
	void setLabel (const std::string& label);

	/** The red text across the middle - the DXi's SetValue, which the
	    property page filled in on a timer with a frequency reading. Empty
	    means the numeric value is shown instead, which is what the DXi
	    did when m_Value was empty. */
	void setValueText (const std::string& text);

	/** The 10 x 10 lamp in the top-left corner - SetUseIndicator. */
	void setUseIndicator (bool use);
	void setIndicator (bool on);
	bool indicator () const { return mIndicator; }

	/** How the numeric value reads when there is no value text. Given the
	    NORMALISED value; the editor hands it the parameter's own
	    formatting so the panel and the host cannot disagree. */
	void setFormatter (std::function<std::string (float)> formatter);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlider, VSTGUI::CControl)

protected:
	void drawLamp (VSTGUI::CDrawContext* context);
	void drawBar (VSTGUI::CDrawContext* context, double fraction, bool fill);
	void drawLabel (VSTGUI::CDrawContext* context, const std::string& text,
	                const VSTGUI::CColor& colour);
	/** Draw `text` centred at the TOP of `band`, dropping a font size
	    rather than letting it run past the edges. */
	void drawFitted (VSTGUI::CDrawContext* context, const std::string& text,
	                 const VSTGUI::CRect& band, const VSTGUI::CColor& colour);

	std::string mLabel;
	std::string mValueText;
	std::function<std::string (float)> mFormatter;
	bool mUseIndicator = false;
	bool mIndicator = false;

	bool mDragging = false;
	VSTGUI::CPoint mLastPoint;
};

//------------------------------------------------------------------------
/** A SlideSpin in two-state mode: a click toggles it.

    The bar fills the whole width when on and disappears when off, and the
    text under it is the name of the state rather than a label - "Use
    Sample" against "Interlace". */
class SpyToggle : public SpySlider
{
public:
	SpyToggle (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	void setStateNames (const std::string& off, const std::string& on);

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpyToggle, SpySlider)

private:
	std::string mNames[2];
};

//------------------------------------------------------------------------
/** A SlideSpin in multi-state mode: an outlined box with the name of the
    current value across it.

    LEFT CLICK STEPS DOWN, RIGHT CLICK STEPS UP, and both wrap. Ctrl-click
    counts as a right click, which is the macOS convention and a fallback
    for hosts that keep the right button to themselves.

    NOT the DXi, which had no click behaviour at all: its vertical mode
    needed the pointer to move 25 pixels before it did anything, on a
    control 18 pixels tall, so the control read as dead until you happened
    to drag it. See ENGINEERING-NOTES section 3.

    The drag is still there and still works the original's way round -
    DOWN ADVANCES, because the DXi decremented a counter it then reported
    as `max - count`, which is the opposite of the usual convention and is
    preserved. The wheel advances upwards, one position per click. */
class SpySelector : public SpySlider
{
public:
	SpySelector (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	void setNames (const std::vector<std::string>& names);

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySelector, SpySlider)

private:
	int currentIndex () const;

	std::vector<std::string> mNames;
	VSTGUI::CCoord mAnchorY = 0.;
	bool mMoved = false;
	bool mStepUp = false;
};

//------------------------------------------------------------------------
/** A momentary push button, for recalling a preset.

    NOT in SpyBand - the nearest thing there was SpyFileButton, which is a
    SlideSpin with its indicator turned on and a click handler on the part
    that is not the lamp. This is that idea with the lamp taken off and
    the parameter taken away.

    It is a CControl only to inherit SpySlider's text fitting; it carries
    NO TAG and never calls valueChanged, beginEdit or endEdit, so a host
    sees nothing when it is clicked except the parameters the handler then
    writes. Every mouse handler is overridden for that reason - SpySlider's
    would drag a value that is not there.

    The click fires on mouse UP, and only if the pointer is still inside:
    pressing a vowel and sliding off it is how you change your mind. */
class SpyPresetButton : public SpySlider
{
public:
	SpyPresetButton (const VSTGUI::CRect& size, const std::string& name);

	void setHandler (std::function<void ()> handler);

	/** Lit, because the host - or this panel - has this vowel selected.
	    A push button that is also a state indicator: the Vowel parameter
	    can be moved by automation with nobody touching the panel, and a
	    row of buttons that did not show which one was live would be
	    lying. */
	void setSelected (bool selected);
	bool selected () const { return mSelected; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpyPresetButton, SpySlider)

private:
	std::string mName;
	std::function<void ()> mHandler;
	bool mPressed = false;
	bool mInside = false;
	bool mSelected = false;
};

//------------------------------------------------------------------------
} // namespace Project6
