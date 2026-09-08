//------------------------------------------------------------------------
// Project6 - the sample slot view, implementation
//------------------------------------------------------------------------

#include "Project6SlotView.h"

#include "Project6Controls.h"

#include <algorithm>
#include <cstring>
#include <string>

using namespace VSTGUI;

namespace Project6 {

namespace {

/** The well. Darker than the panel, so an empty slot reads as a hole
    rather than as a raised button nobody has labelled. */
const CColor kWellFill     ( 40,  40,  40, 255);
/** Loaded slots are a shade lighter, so a full row and an empty one are
    told apart at a glance and not only by reading sixty-four names. */
const CColor kWellFillFull ( 58,  58,  58, 255);
/** PLAYING. A tint across the whole well rather than a lamp in the
    corner: a lamp would cost the name several characters of a control
    that has few to spare, and on a grid of sixty-four cells the eye finds
    a block of colour faster than it finds four lit dots. */
const CColor kWellFillLive ( 96,  34,  34, 255);
/** ARMED AND WAITING for the bar line. Dimmer than playing and warmer
    than idle: something has been asked for and has not happened yet. */
const CColor kWellFillArmed( 74,  60,  30, 255);
/** Recessed: the DXi's Draw3dRect colours the OTHER WAY ROUND from the
    raised bar in Project6Controls.cpp. Same two values, opposite corners. */
const CColor kWellShadow   (100, 100, 100, 255);
const CColor kWellHigh     (200, 200, 200, 255);

/** WHITE, as asked for - not the near-white Colours::kValue the sliders
    use for their readouts. A slot's name is the only thing in its well,
    with nothing to compete with, and the extra contrast is what makes a
    grid of sixty-four of them scannable. */
const CColor kSlotText     (255, 255, 255, 255);
/** A file that will not play. Muted and warm, so the slot reads as "there
    is something wrong with this one" rather than as an empty slot or a
    working one. The tooltip says what. */
const CColor kSlotTextBad  (208, 132, 132, 255);

/** The border while the loop is running. The DXi's own lamp red. */
const CColor kLiveBorder   (255,  70,  70, 255);
/** The border while a change is waiting for the next bar line - in
    EITHER direction, because "about to start" and "about to stop" are the
    same fact about the same slot and want the same mark. */
const CColor kPendingBorder(255, 190,  60, 255);
/** The frame while an acceptable file is over the slot. Green, the
    panel's own label colour, so "this one, and yes" is one glance. */
const CColor kDropAccept   ( 50, 255,  50, 255);

/** Inset from the well's edge to the text. */
constexpr CCoord kTextInset = 4.;

//------------------------------------------------------------------------
/** A recessed rectangle: dark along the top and left, light along the
    bottom and right. The inverse of the raised bar the sliders draw.

    Deliberately local. Project6Controls.cpp has a draw3dRect of its own
    and this could have been a call to it - but that file is kept
    byte-comparable with SpyBand's and VocalFilter's copies, and exporting
    a helper out of it would mean editing its header. */
void drawWell (CDrawContext* context, const CRect& r,
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
SpySampleSlot::SpySampleSlot (const CRect& size, IControlListener* listener,
                              int32_t tag, int index)
: CControl (size, listener, tag)
, mIndex (index)
{
	// A view with the mouse turned off is skipped by the frame's hit test,
	// and the drag never reaches getDropTarget() - so an empty slot has to
	// stay mouse-enabled even though a click on it does nothing. That is
	// the whole control gone, from one line that looks like tidying up.
	setMouseEnabled (true);

	// Two states, no travel between them.
	setMin (0.f);
	setMax (1.f);
	setValueNormalized (0.f);
}

//------------------------------------------------------------------------
void SpySampleSlot::setPath (const std::string& path)
{
	if (path == mPath)
		return;

	mPath = path;
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySampleSlot::setStatus (SampleStatus status)
{
	if (status == mStatus)
		return;

	mStatus = status;
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySampleSlot::setSounding (bool sounding)
{
	if (sounding == mSounding)
		return;

	mSounding = sounding;
	invalid ();
}

//------------------------------------------------------------------------
void SpySampleSlot::refreshTooltip ()
{
	// The full path, because the name on the slot is shortened and two
	// takes of the same sample usually differ only in the directory - and
	// the reason it will not play, when there is one, because a slot that
	// refuses to start has to be able to say why somewhere.
	if (mPath.empty ())
	{
		setTooltipText (nullptr);
		return;
	}

	std::string text = mPath;
	if (mStatus != SampleStatus::Loaded && mStatus != SampleStatus::Empty)
	{
		text += "\n";
		text += sampleStatusText (mStatus);
	}

	setTooltipText (text.c_str ());
}

//------------------------------------------------------------------------
void SpySampleSlot::setHandler (std::function<void (int, const std::string&)> handler)
{
	mHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SpySampleSlot::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// Consumed either way, so a click on an empty slot does not fall
	// through to the frame and do something else instead.
	event.consumed = true;

	// NOTHING TO PLAY. Lighting the well for a slot that cannot make a
	// sound would be the exact failure the status message exists to
	// prevent - and the tooltip already says why.
	if (! playable ())
		return;

	// A COMPLETE EDIT GESTURE, exactly as SpyToggle does it: begin, set,
	// notify, end. That is what makes the host treat this as something it
	// can record and undo, and what carries the change to the processor
	// and to any other editor open on this instance.
	beginEdit ();
	setValueNormalized (armed () ? 0.f : 1.f);
	valueChanged ();
	endEdit ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySampleSlot::onMouseWheelEvent (MouseWheelEvent& event)
{
	// Swallowed on purpose - see the header. A wheel over a pad grid
	// would otherwise start and stop loops as the pointer passed over.
	event.consumed = true;
}

//------------------------------------------------------------------------
std::string SpySampleSlot::firstAcceptedPath (IDataPackage* package)
{
	if (package == nullptr)
		return std::string ();

	const uint32_t count = package->getCount ();
	for (uint32_t i = 0; i < count; ++i)
	{
		const void* buffer = nullptr;
		IDataPackage::Type type = IDataPackage::kError;
		const uint32_t size = package->getData (i, buffer, type);

		// kText is what a drag from a text editor looks like, and kBinary
		// is anything else. Only a real file has a path to remember.
		if (type != IDataPackage::kFilePath || buffer == nullptr || size == 0)
			continue;

		// UTF-8, and the size MAY OR MAY NOT include the terminator
		// depending on the platform layer that packed it. Taking the
		// length with strnlen handles both; constructing from `size`
		// alone would leave a trailing NUL inside the string, which then
		// compares unequal to the same path read back out of a project
		// file and quietly breaks every comparison downstream.
		const char* chars = static_cast<const char*> (buffer);
		const std::string path (chars, ::strnlen (chars, size));

		if (isAcceptedSampleFile (path))
			return path;
	}

	return std::string ();
}

//------------------------------------------------------------------------
DragOperation SpySampleSlot::onDragEnter (DragEventData data)
{
	// The acceptance test runs HERE, not only on the drop, so the pointer
	// says no before the mouse button is released. A slot that lights up
	// for an .mp3 and then silently ignores it is worse than one that
	// never lit up.
	mDragOver = ! firstAcceptedPath (data.drag).empty ();
	invalid ();

	return mDragOver ? DragOperation::Copy : DragOperation::None;
}

//------------------------------------------------------------------------
DragOperation SpySampleSlot::onDragMove (DragEventData data)
{
	// The answer cannot change while the pointer is inside one slot: the
	// package is the same package. Recomputing it on every mouse move
	// would re-scan the whole drag hundreds of times a second.
	return mDragOver ? DragOperation::Copy : DragOperation::None;
}

//------------------------------------------------------------------------
void SpySampleSlot::onDragLeave (DragEventData data)
{
	if (! mDragOver)
		return;

	mDragOver = false;
	invalid ();
}

//------------------------------------------------------------------------
bool SpySampleSlot::onDrop (DragEventData data)
{
	// VSTGUI does NOT call onDragLeave after a drop, so the highlight has
	// to be taken off here or the slot stays lit for ever.
	mDragOver = false;
	invalid ();

	const std::string path = firstAcceptedPath (data.drag);
	if (path.empty ())
		return false;

	// mPath is NOT set here. The handler writes through the controller,
	// which writes back to this slot - and to the same slot in any other
	// editor the host has open on this instance. Setting it here as well
	// would make the panel right for one window and by luck for the rest.
	if (mHandler)
		mHandler (mIndex, path);

	return true;
}

//------------------------------------------------------------------------
void SpySampleSlot::drawName (CDrawContext* context, const CRect& band)
{
	if (mPath.empty ())
		return;

	const std::string full = slotFileName (mPath);
	const std::string stem = slotFileStem (mPath);

	// In order of what is worth losing. The extension goes first: every
	// file in a slot is a .wav, so those four characters are the ones a
	// reader already knows. Only then does the type get smaller, and only
	// then are characters actually cut.
	struct Attempt { const std::string* text; CFontRef font; };
	const Attempt attempts[] =
	{
		{ &full, panelFont () },
		{ &stem, panelFont () },
		{ &stem, panelFontSmall () },
		{ &stem, panelFontTiny () },
	};

	const CCoord room = band.getWidth ();

	std::string shown;
	CFontRef font = panelFontTiny ();

	for (const auto& attempt : attempts)
	{
		context->setFont (attempt.font);
		shown = *attempt.text;
		font  = attempt.font;
		if (context->getStringWidth (shown.c_str ()) <= room)
			break;
	}

	// Still too wide: cut from the END and mark the cut. A name is
	// identified by its beginning far more often than by its ending -
	// "kick soft 03" and "kick soft 04" being the exception rather than
	// the rule - so the front is what survives.
	context->setFont (font);
	if (context->getStringWidth (shown.c_str ()) > room)
	{
		static const char* const kEllipsis = "\xE2\x80\xA6";     // U+2026
		while (! shown.empty ()
		       && context->getStringWidth ((shown + kEllipsis).c_str ()) > room)
			shown.pop_back ();

		shown += kEllipsis;
	}

	const CCoord height = font->getSize () + 2.;
	const CRect line (band.left,
	                  band.top + (band.getHeight () - height) * 0.5,
	                  band.right,
	                  band.top + (band.getHeight () - height) * 0.5 + height);

	// A file that will not play says so in its colour as well as in its
	// tooltip: a name in white is a name you can click.
	context->setFontColor (playable () ? kSlotText : kSlotTextBad);
	context->drawString (shown.c_str (), line, kCenterText, true);
}

//------------------------------------------------------------------------
void SpySampleSlot::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	// THREE STATES, from two facts. Sounding is what the processor says is
	// happening; armed is what was clicked. A slot that is armed and not
	// yet sounding is waiting for the bar line - and so is one that is
	// sounding and no longer armed.
	const bool sounding = mSounding && playable ();
	const bool waiting  = playable () && pending ();

	// The well itself.
	context->setFillColor (sounding ? kWellFillLive
	                                : (waiting ? kWellFillArmed
	                                           : (mPath.empty () ? kWellFill : kWellFillFull)));
	context->drawRect (r, kDrawFilled);
	drawWell (context, r, kWellShadow, kWellHigh);

	// An edge, for whichever of the three things is true. They are drawn
	// inside the well's own edge so the two do not fight, and the order is
	// what is most immediate: a drag is happening now, a pending change is
	// about to, and playing is already.
	if (mDragOver || waiting || sounding)
	{
		CRect highlight (r);
		highlight.inset (1., 1.);
		context->setLineWidth (1.);
		context->setFrameColor (mDragOver ? kDropAccept
		                                  : (waiting ? kPendingBorder : kLiveBorder));
		context->drawRect (highlight, kDrawStroked);
	}

	// AN EMPTY SLOT IS BLANK. No index, no "drop a file here", nothing:
	// sixty-four copies of any of those is a wall of text with the four
	// names that matter hidden in it.
	CRect band (r);
	band.inset (kTextInset, 2.);
	drawName (context, band);

	setDirty (false);
}

//------------------------------------------------------------------------
// SpyColumnButton
//------------------------------------------------------------------------

SpyColumnButton::SpyColumnButton (const CRect& size, int column)
: CView (size)
, mColumn (column)
{
	setMouseEnabled (true);

	// "Play column 3", so the number on the face does not have to also
	// explain itself.
	const std::string tip = "Play column " + std::to_string (column + 1);
	setTooltipText (tip.c_str ());
}

//------------------------------------------------------------------------
void SpyColumnButton::setHandler (std::function<void (int)> handler)
{
	mHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SpyColumnButton::setState (int playable, int sounding, bool pending)
{
	if (playable == mPlayable && sounding == mSounding && pending == mPending)
		return;

	mPlayable = playable;
	mSounding = sounding;
	mPending  = pending;
	invalid ();
}

//------------------------------------------------------------------------
void SpyColumnButton::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// Consumed either way, so a press on an empty column does not fall
	// through to the frame and do something else instead.
	event.consumed = true;

	// NOTHING LOADED IN THIS COLUMN. Lighting it would promise eight
	// loops that do not exist.
	if (mPlayable <= 0)
		return;

	if (mHandler)
		mHandler (mColumn);
}

//------------------------------------------------------------------------
void SpyColumnButton::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	const bool live = (mPlayable > 0 && mSounding > 0);

	// The ground, and then a FILL PROPORTIONAL to how much of the column
	// is actually sounding - the DXi's own bar idiom. Three of eight
	// playing is three eighths lit, which says at a glance what a lamp
	// could only say as "some".
	context->setFillColor (mPlayable > 0 ? kWellFillFull : kWellFill);
	context->drawRect (r, kDrawFilled);

	if (live)
	{
		CRect fill (r);
		fill.inset (1., 1.);
		fill.right = fill.left + fill.getWidth ()
		                             * (static_cast<double> (mSounding)
		                                / static_cast<double> (mPlayable));
		if (fill.getWidth () > 0.)
		{
			context->setFillColor (kWellFillLive);
			context->drawRect (fill, kDrawFilled);
		}
	}

	// RAISED, not recessed: this one is a button rather than a well, so
	// the two edge colours go the way round the sliders use.
	drawWell (context, r, kWellHigh, kWellShadow);

	// The same edge rule as a slot: amber while anything in the column is
	// waiting for a bar line, red once it is all running.
	if (mPending || (mPlayable > 0 && mSounding == mPlayable))
	{
		CRect highlight (r);
		highlight.inset (1., 1.);
		context->setLineWidth (1.);
		context->setFrameColor (mPending ? kPendingBorder : kLiveBorder);
		context->drawRect (highlight, kDrawStroked);
	}

	// The column's number, matching the second half of a slot's name -
	// "Slot C6" is row C, column 6, and this is the 6.
	context->setFont (panelFont ());
	context->setFontColor (mPlayable > 0 ? kSlotText : kSlotTextBad);

	const CCoord height = panelFont ()->getSize () + 2.;
	const CRect line (r.left,
	                  r.top + (r.getHeight () - height) * 0.5,
	                  r.right,
	                  r.top + (r.getHeight () - height) * 0.5 + height);

	context->drawString (std::to_string (mColumn + 1).c_str (), line, kCenterText, true);

	setDirty (false);
}

//------------------------------------------------------------------------
} // namespace Project6
