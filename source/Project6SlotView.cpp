//------------------------------------------------------------------------
// Project6 - the sample slot view, implementation
//------------------------------------------------------------------------

#include "Project6SlotView.h"

#include "Project6Controls.h"

#include <algorithm>
#include <cstring>

using namespace VSTGUI;

namespace Project6 {

namespace {

/** The well. Darker than the panel, so an empty slot reads as a hole
    rather than as a raised button nobody has labelled. */
const CColor kWellFill    ( 40,  40,  40, 255);
/** Loaded slots are a shade lighter, so a full row and an empty one are
    told apart at a glance and not only by reading sixty-four names. */
const CColor kWellFillFull( 58,  58,  58, 255);
/** Recessed: the DXi's Draw3dRect colours the OTHER WAY ROUND from the
    raised bar in Project6Controls.cpp. Same two values, opposite corners. */
const CColor kWellShadow  (100, 100, 100, 255);
const CColor kWellHigh    (200, 200, 200, 255);

/** WHITE, as asked for - not the near-white Colours::kValue the sliders
    use for their readouts. A slot's name is the only thing in its well,
    with nothing to compete with, and the extra contrast is what makes a
    grid of sixty-four of them scannable. */
const CColor kSlotText    (255, 255, 255, 255);

/** The frame while a file the plug-in will take is over the slot. Green,
    the panel's own label colour, so "this one, and yes" is one glance. */
const CColor kDropAccept  ( 50, 255,  50, 255);

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
SpySampleSlot::SpySampleSlot (const CRect& size, int index)
: CView (size)
, mIndex (index)
{
	// A slot has no click behaviour, but it MUST stay mouse-enabled: a
	// view with the mouse turned off is skipped by the frame's hit test,
	// and the drag never reaches getDropTarget(). That is the whole
	// control gone, from one line that looks like tidying up.
	setMouseEnabled (true);
}

//------------------------------------------------------------------------
void SpySampleSlot::setPath (const std::string& path)
{
	if (path == mPath)
		return;

	mPath = path;

	// The full path as a tooltip, because the name on the slot is
	// shortened and two takes of the same sample usually differ in the
	// directory. Costs nothing when the frame has tooltips off.
	setTooltipText (mPath.empty () ? nullptr : mPath.c_str ());

	invalid ();
}

//------------------------------------------------------------------------
void SpySampleSlot::setHandler (std::function<void (int, const std::string&)> handler)
{
	mHandler = std::move (handler);
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
		if (context->getStringWidth (attempt.text->c_str ()) <= room)
		{
			shown = *attempt.text;
			font  = attempt.font;
			break;
		}
		// Remember the smallest attempt, so the truncation below works on
		// the version that fits the most characters in.
		shown = *attempt.text;
		font  = attempt.font;
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

	context->setFontColor (kSlotText);
	context->drawString (shown.c_str (), line, kCenterText, true);
}

//------------------------------------------------------------------------
void SpySampleSlot::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	// The well itself.
	context->setFillColor (mPath.empty () ? kWellFill : kWellFillFull);
	context->drawRect (r, kDrawFilled);
	drawWell (context, r, kWellShadow, kWellHigh);

	// An acceptable file is over this slot: outline it, inside the well's
	// own edge so the two do not fight.
	if (mDragOver)
	{
		CRect highlight (r);
		highlight.inset (1., 1.);
		context->setLineWidth (1.);
		context->setFrameColor (kDropAccept);
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
} // namespace Project6
