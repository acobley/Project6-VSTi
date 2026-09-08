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

#include "Project6Controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace Project6 {

namespace {

/** The DXi's bar geometry, in pixels from the control's own edges
    (SlideSpin::PaintBk). */
constexpr CCoord kBarBottomInset = 3.;
constexpr CCoord kBarHeight      = 12.;   // rect.bottom-15 .. rect.bottom-3
constexpr CCoord kLabelTop       = 17.;   // rect.bottom-17
constexpr CCoord kLabelBottom    = 6.;    // rect.bottom-6
constexpr CCoord kLampSize       = 10.;

/** The DXi moved one unit of a 0..100 range per pixel. */
constexpr float kUnitsPerPixel = 1.f / 100.f;

/** The vertical selector needed 25 pixels of movement per step. */
constexpr CCoord kSelectorStep = 25.;

/** MFC's Draw3dRect: a light line down the top and left, a highlight up
    the bottom and right. */
void draw3dRect (CDrawContext* context, const CRect& r,
                 const CColor& topLeft, const CColor& bottomRight)
{
	if (r.getWidth () <= 0. || r.getHeight () <= 0.)
		return;

	context->setLineWidth (1.);
	context->setFrameColor (topLeft);
	context->drawLine (CPoint (r.left, r.top), CPoint (r.right - 1., r.top));
	context->drawLine (CPoint (r.left, r.top), CPoint (r.left, r.bottom - 1.));

	context->setFrameColor (bottomRight);
	context->drawLine (CPoint (r.left, r.bottom - 1.), CPoint (r.right - 1., r.bottom - 1.));
	context->drawLine (CPoint (r.right - 1., r.top), CPoint (r.right - 1., r.bottom - 1.));
}

} // namespace

//------------------------------------------------------------------------
CFontRef panelFont ()
{
	// MFC's CreatePointFont(80) is Arial at 8 points, which at the 96 dpi
	// the dialog was designed for is 11 pixels.
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Arial", 11);
	return font;
}

CFontRef panelFontSmall ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Arial", 10);
	return font;
}

CFontRef panelFontTiny ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Arial", 9);
	return font;
}

//------------------------------------------------------------------------
// SpySlider
//------------------------------------------------------------------------
SpySlider::SpySlider (const CRect& size, IControlListener* listener, int32_t tag)
: CControl (size, listener, tag, nullptr)
{
	setWantsFocus (true);
}

void SpySlider::setLabel (const std::string& label)
{
	if (mLabel == label)
		return;
	mLabel = label;
	invalid ();
}

void SpySlider::setValueText (const std::string& text)
{
	if (mValueText == text)
		return;
	mValueText = text;
	invalid ();
}

void SpySlider::setUseIndicator (bool use)
{
	mUseIndicator = use;
	invalid ();
}

void SpySlider::setIndicator (bool on)
{
	if (mIndicator == on)
		return;
	mIndicator = on;
	invalid ();
}

void SpySlider::setFormatter (std::function<std::string (float)> formatter)
{
	mFormatter = std::move (formatter);
}

//------------------------------------------------------------------------
void SpySlider::drawLamp (CDrawContext* context)
{
	if (! mUseIndicator)
		return;

	const CRect r = getViewSize ();
	CRect lamp (r.left, r.top, r.left + kLampSize, r.top + kLampSize);
	draw3dRect (context, lamp, Colours::kLampFrame, Colours::kLampFrame);

	lamp.inset (2., 2.);
	context->setFillColor (mIndicator ? Colours::kLampOn : Colours::kLampOff);
	context->drawRect (lamp, kDrawFilled);
}

void SpySlider::drawBar (CDrawContext* context, double fraction, bool fill)
{
	const CRect r = getViewSize ();

	CRect bar (r.left,
	           r.bottom - kBarBottomInset - kBarHeight,
	           r.left + r.getWidth () * std::clamp (fraction, 0.0, 1.0),
	           r.bottom - kBarBottomInset);

	draw3dRect (context, bar, Colours::kBarLight, Colours::kBarHigh);

	if (! fill)
		return;

	bar.inset (1., 1.);
	if (bar.getWidth () <= 0. || bar.getHeight () <= 0.)
		return;
	context->setFillColor (Colours::kBarFill);
	context->drawRect (bar, kDrawFilled);
}

void SpySlider::drawFitted (CDrawContext* context, const std::string& text,
                            const CRect& band, const CColor& colour)
{
	if (text.empty ())
		return;

	// Windows drew both of these with DT_CENTER and no DT_VCENTER, so they
	// sat at the TOP of the band they were given, not in the middle of it.
	// Centring them vertically instead puts the red value straight through
	// the green label - which is what the first render of this panel showed.
	CFontRef font = panelFont ();
	context->setFont (font);
	if (context->getStringWidth (text.c_str ()) > band.getWidth ())
	{
		font = panelFontSmall ();
		context->setFont (font);
		if (context->getStringWidth (text.c_str ()) > band.getWidth ())
		{
			font = panelFontTiny ();
			context->setFont (font);
		}
	}

	const CCoord height = font->getSize () + 2.;
	const CRect line (band.left, band.top, band.right,
	                  std::min (band.top + height, band.bottom));

	context->setFontColor (colour);
	context->drawString (text.c_str (), line, kCenterText, true);
}

void SpySlider::drawLabel (CDrawContext* context, const std::string& text,
                           const CColor& colour)
{
	const CRect r = getViewSize ();
	drawFitted (context, text,
	            CRect (r.left, r.bottom - kLabelTop, r.right, r.bottom), colour);
}

//------------------------------------------------------------------------
void SpySlider::draw (CDrawContext* context)
{
	// The panel bitmap behind the control shows through: the DXi blitted
	// its parent's pixels and drew on top, and here the frame's background
	// has already been drawn under us. Nothing is painted over it but the
	// bar, the text and the lamp.
	drawBar (context, getValueNormalized (), true);
	drawLabel (context, mLabel, Colours::kLabel);
	drawLamp (context);

	std::string value = mValueText;
	if (value.empty () && mFormatter)
		value = mFormatter (getValueNormalized ());

	// The value goes at the TOP of the control and the label at the
	// bottom, which is where DT_CENTER without DT_VCENTER put them.
	drawFitted (context, value, getViewSize (), Colours::kValue);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpySlider::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// No absolute positioning: the DXi's slider moved by increments from
	// wherever it was, and on a control 69 pixels wide jumping to the
	// pointer would make every setting a coarse one.
	mDragging = true;
	mLastPoint = event.mousePosition;
	beginEdit ();
	event.consumed = true;
}

void SpySlider::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mDragging)
		return;

	const CCoord dx = event.mousePosition.x - mLastPoint.x;
	if (std::fabs (dx) < 1.)
		return;

	mLastPoint = event.mousePosition;

	const float scale = event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f;
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (dx) * kUnitsPerPixel * scale,
		0.f, 1.f));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void SpySlider::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;
	mDragging = false;
	endEdit ();
	event.consumed = true;
}

void SpySlider::onMouseCancelEvent (MouseCancelEvent& event)
{
	if (mDragging)
	{
		mDragging = false;
		endEdit ();
	}
	event.consumed = true;
}

void SpySlider::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float step = event.modifiers.has (ModifierKey::Shift) ? 0.002f : 0.01f;
	beginEdit ();
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (event.deltaY) * step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// SpyToggle
//------------------------------------------------------------------------
SpyToggle::SpyToggle (const CRect& size, IControlListener* listener, int32_t tag)
: SpySlider (size, listener, tag)
{
}

void SpyToggle::setStateNames (const std::string& off, const std::string& on)
{
	mNames[0] = off;
	mNames[1] = on;
	invalid ();
}

void SpyToggle::draw (CDrawContext* context)
{
	const bool on = getValueNormalized () >= 0.5f;

	// All or nothing: the DXi's two-state scale was 1.0 or 0.0, never
	// anything between.
	drawBar (context, on ? 1.0 : 0.0, true);

	// A two-state control shows the NAME OF ITS STATE where a slider shows
	// its label, and shows no red value text at all.
	drawLabel (context, mNames[on ? 1 : 0], Colours::kLabel);
	drawLamp (context);

	setDirty (false);
}

void SpyToggle::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	beginEdit ();
	setValueNormalized (getValueNormalized () >= 0.5f ? 0.f : 1.f);
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

void SpyToggle::onMouseMoveEvent (MouseMoveEvent& event)
{
	// The DXi returned early from OnMouseMove for a two-state control, so
	// a drag across one does nothing.
	event.consumed = false;
}

void SpyToggle::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

void SpyToggle::onMouseWheelEvent (MouseWheelEvent& event)
{
	// SlideSpin::OnMouseWheel returns immediately for a two-state control,
	// and so does this. WITHOUT the override the inherited slider wheel
	// nudges a switch by a hundredth of its travel per click, so it takes
	// fifty clicks to flip and lands the parameter on values a two-state
	// control has no business holding.
	event.consumed = false;
}

//------------------------------------------------------------------------
// SpySelector
//------------------------------------------------------------------------
SpySelector::SpySelector (const CRect& size, IControlListener* listener, int32_t tag)
: SpySlider (size, listener, tag)
{
}

void SpySelector::setNames (const std::vector<std::string>& names)
{
	mNames = names;
	invalid ();
}

int SpySelector::currentIndex () const
{
	if (mNames.empty ())
		return 0;
	const int last = static_cast<int> (mNames.size ()) - 1;
	if (last <= 0)
		return 0;
	return std::clamp (static_cast<int> (getValueNormalized () * last + 0.5f), 0, last);
}

void SpySelector::draw (CDrawContext* context)
{
	// A multi-state control is a box the full height of the view with the
	// name of the current value across it: PaintBk took Bar.top from
	// rect.top and skipped the fill.
	const CRect r = getViewSize ();
	draw3dRect (context, CRect (r.left, r.top, r.right, r.bottom - kBarBottomInset),
	            Colours::kBarLight, Colours::kBarHigh);

	if (! mNames.empty ())
		drawFitted (context, mNames[static_cast<std::size_t> (currentIndex ())],
		            r, Colours::kValue);

	setDirty (false);
}

void SpySelector::onMouseDownEvent (MouseDownEvent& event)
{
	const bool right = event.buttonState.isRight ()
	                || event.modifiers.has (ModifierKey::Control);

	if (! event.buttonState.isLeft () && ! right)
		return;

	// Which way a click without a drag will step. Ctrl counts as a right
	// click: it is the macOS convention, and it is the fallback for a host
	// that keeps the right button for its own menu.
	mStepUp = right;
	mDragging = true;
	mMoved = false;
	mAnchorY = event.mousePosition.y;
	beginEdit ();
	event.consumed = true;
}

void SpySelector::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mDragging || mNames.size () < 2)
		return;

	const int last = static_cast<int> (mNames.size ()) - 1;
	int index = currentIndex ();

	// DOWN ADVANCES. The DXi decremented its counter when the pointer went
	// down, and reported max - counter, so down raised the value. Kept.
	if (event.mousePosition.y > mAnchorY + kSelectorStep)
	{
		mAnchorY = event.mousePosition.y;
		mMoved = true;
		index = std::min (index + 1, last);
	}
	else if (event.mousePosition.y < mAnchorY - kSelectorStep)
	{
		mAnchorY = event.mousePosition.y;
		mMoved = true;
		index = std::max (index - 1, 0);
	}
	else
	{
		return;
	}

	setValueNormalized (static_cast<float> (index) / static_cast<float> (last));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void SpySelector::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;

	// A click that did not drag steps one position: LEFT DOWN, RIGHT UP,
	// both wrapping, so either button alone can reach every value. The DXi
	// had nothing here, which is why the control read as dead - its only
	// way in was a 25-pixel drag on an 18-pixel control. The drag is
	// untouched; this is purely additional. The same click-versus-drag
	// test the patch board uses - did the value actually move? - rather
	// than a timer.
	if (! mMoved && mNames.size () > 1)
	{
		const int positions = static_cast<int> (mNames.size ());
		const int index = (currentIndex () + (mStepUp ? 1 : positions - 1)) % positions;
		setValueNormalized (static_cast<float> (index)
		                    / static_cast<float> (positions - 1));
		valueChanged ();
		invalid ();
	}

	mDragging = false;
	mMoved = false;
	endEdit ();
	event.consumed = true;
}

void SpySelector::onMouseWheelEvent (MouseWheelEvent& event)
{
	// ONE STEP PER CLICK, which is what SlideSpin::OnMouseWheel did:
	// `count--` or `count++`, a whole position at a time.
	//
	// Without this the inherited slider wheel moved a hundredth of the
	// control's travel per click - and a four-position selector's step is
	// a THIRD of its travel, so it took seventeen clicks to change from
	// 9 Bands to 12 and left the parameter on values between the steps.
	// Wheel up raises the value, as it did.
	if (mNames.size () < 2)
		return;

	const int last = static_cast<int> (mNames.size ()) - 1;
	int index = currentIndex ();

	if (event.deltaY > 0.)
		index = std::min (index + 1, last);
	else if (event.deltaY < 0.)
		index = std::max (index - 1, 0);
	else
		return;

	beginEdit ();
	setValueNormalized (static_cast<float> (index) / static_cast<float> (last));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// SpyPresetButton
//------------------------------------------------------------------------
SpyPresetButton::SpyPresetButton (const CRect& size, const std::string& name)
: SpySlider (size, nullptr, -1)
, mName (name)
{
}

void SpyPresetButton::setHandler (std::function<void ()> handler)
{
	mHandler = std::move (handler);
}

void SpyPresetButton::setSelected (bool selected)
{
	if (selected == mSelected)
		return;
	mSelected = selected;
	invalid ();
}

//------------------------------------------------------------------------
void SpyPresetButton::draw (CDrawContext* context)
{
	// The same outlined box a multi-state SlideSpin draws, so a row of
	// these sits on the panel as though the DXi had always had them.
	const CRect r = getViewSize ();
	const CRect box (r.left, r.top, r.right, r.bottom - kBarBottomInset);

	// Pressed swaps the 3d rect's two edges, which is what Windows did to
	// show a button down, and fills it so the state is obvious on a dark
	// panel where a one-pixel edge is not.
	const bool down = (mPressed && mInside);

	if (down || mSelected)
	{
		draw3dRect (context, box, Colours::kBarHigh, Colours::kBarLight);
		CRect fill (box);
		fill.inset (1., 1.);
		if (fill.getWidth () > 0. && fill.getHeight () > 0.)
		{
			context->setFillColor (Colours::kBarFill);
			context->drawRect (fill, kDrawFilled);
		}
	}
	else
	{
		draw3dRect (context, box, Colours::kBarLight, Colours::kBarHigh);
	}

	// Green for the live vowel, the panel's colour for a label that is
	// stating a fact; red otherwise, the colour it uses for a value you
	// can change.
	drawFitted (context, mName, r,
	            (down || mSelected) ? Colours::kLabel : Colours::kValue);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpyPresetButton::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	mPressed = true;
	mInside = true;
	invalid ();
	event.consumed = true;
}

void SpyPresetButton::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mPressed)
		return;

	const bool inside = getViewSize ().pointInside (event.mousePosition);
	if (inside != mInside)
	{
		mInside = inside;
		invalid ();
	}
	event.consumed = true;
}

void SpyPresetButton::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mPressed)
		return;

	const bool fire = mInside && getViewSize ().pointInside (event.mousePosition);
	mPressed = false;
	mInside = false;
	invalid ();
	event.consumed = true;

	// LAST, because the handler rewrites nine parameters and the frame may
	// well be redrawn out from under this call.
	if (fire && mHandler)
		mHandler ();
}

void SpyPresetButton::onMouseCancelEvent (MouseCancelEvent& event)
{
	mPressed = false;
	mInside = false;
	invalid ();
	event.consumed = true;
}

void SpyPresetButton::onMouseWheelEvent (MouseWheelEvent&)
{
	// Nothing. A wheel over a push button should not do anything, and
	// SpySlider's would move a value that is not there.
}

//------------------------------------------------------------------------
} // namespace Project6
