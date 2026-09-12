//------------------------------------------------------------------------
// Project6 - the sample slot view, implementation
//------------------------------------------------------------------------

#include "Project6SlotView.h"

#include "Project6Controls.h"
#include "Project6Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
/** A SETTING THAT IS NOT DOING ANYTHING: the fit mode on a pad whose file
    has no tempo, or is a one-shot, or matches the project already. Still
    legible - it is a real setting and it will apply the moment a file
    with a tempo lands here - but visibly not in force. */
const CColor kSlotTextIdle (140, 140, 140, 255);

/** A MIDI pad's well. Cooler and a shade violet against the neutral grey
    of an audio one - far enough apart to pick out of a grid of sixty-four
    at a glance, close enough that a bank of both does not look like two
    different plug-ins. */
const CColor kWellFillMidi (58, 56, 74, 255);

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

/** The playhead bar along the bottom of a playing pad. Three pixels: it
    has to be seen from across a room of sixty-four cells and must not
    become a second control. */
constexpr CCoord kProgressHeight = 3.;

/** Bright, and the same warm red as the live border it sits inside, so
    the bar reads as part of the pad being lit rather than as a fifth
    thing on the panel. */
const CColor kProgressBar (255, 140, 110, 255);

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
void SpySampleSlot::setOverlay (const std::string& text)
{
	if (text == mOverlay)
		return;

	mOverlay = text;
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
void SpySampleSlot::setTempoText (const std::string& text)
{
	if (text == mTempoText)
		return;

	mTempoText = text;
	refreshTooltip ();
}

//------------------------------------------------------------------------
void SpySampleSlot::setKind (SlotFileKind kind)
{
	if (kind == mKind)
		return;

	mKind = kind;
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
void SpySampleSlot::setProgress (float progress)
{
	const float clamped = std::min (1.f, std::max (0.f, progress));

	// QUANTISED TO THE PIXEL it will actually be drawn at. This arrives
	// thirty times a second for sixty-four pads; redrawing one because a
	// value moved a thousandth, when the bar cannot move less than a
	// pixel, is sixty-four redraws a second to change nothing.
	const CCoord width = std::max (1., getViewSize ().getWidth () - 2.);
	const float step = static_cast<float> (1.0 / width);

	if (std::fabs (clamped - mProgress) < step && clamped != 0.f && mProgress != 0.f)
		return;

	mProgress = clamped;
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
	else if (!mTempoText.empty ())
	{
		// Only on a file that actually loaded: a tempo beside "not a WAV
		// file" would be a number about a file that was never read.
		text += "\n";
		text += mTempoText;
	}

	// A pad that can be picked up SAYS SO. Nothing on the panel looks
	// draggable, and a gesture nobody knows about is a gesture nobody
	// uses - the tooltip is already open in front of the only people who
	// would want it.
	text += "\ndrag to another pad to move it, hold Ctrl or Alt to copy";

	setTooltipText (text.c_str ());
}

//------------------------------------------------------------------------
void SpySampleSlot::setHandler (std::function<void (int, const std::string&)> handler)
{
	mHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SpySampleSlot::setMoveHandler (std::function<void (int, int, bool)> handler)
{
	mMoveHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SpySampleSlot::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// NOTHING HAPPENS YET. The press could be a click, which launches the
	// pad, or the start of a drag, which moves its file - and there is no
	// way to know which until the pointer moves or the button comes up.
	// See the header for why the launch moved off the press.
	//
	// Consumed either way: it is what makes the frame send this view the
	// move and up events that follow, and it stops a click on an empty
	// slot falling through to the frame.
	mPressed     = true;
	mDragStarted = false;
	mPressPoint  = event.mousePosition;
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpySampleSlot::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mPressed || mDragStarted)
		return;

	event.consumed = true;

	// AN EMPTY SLOT HAS NOTHING TO PICK UP. The press stays a press, so
	// releasing on it still does what a click on an empty slot does,
	// which is nothing.
	if (mPath.empty ())
		return;

	const CCoord dx = event.mousePosition.x - mPressPoint.x;
	const CCoord dy = event.mousePosition.y - mPressPoint.y;
	if (std::fabs (dx) < kDragThreshold && std::fabs (dy) < kDragThreshold)
		return;

	// Past the threshold: this is a drag. Clearing mPressed FIRST means
	// the release that ends the drag cannot also be read as a click -
	// the platform may or may not deliver one, and a pad that launched
	// itself at the end of every move would be unusable.
	mPressed     = false;
	mDragStarted = true;
	beginSlotDrag ();
}

//------------------------------------------------------------------------
void SpySampleSlot::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mPressed)
	{
		// The tail of a drag, or a release this view never saw the press
		// for. Either way it is not a click.
		mDragStarted = false;
		return;
	}

	mPressed = false;
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
void SpySampleSlot::onMouseCancelEvent (MouseCancelEvent&)
{
	// A cancelled press is not a click. Without this a pad would launch
	// when a modal window stole the mouse mid-press.
	mPressed     = false;
	mDragStarted = false;
}

//------------------------------------------------------------------------
void SpySampleSlot::beginSlotDrag ()
{
	const std::string payload = encodeSlotDrag (mIndex, mPath);
	if (payload.empty ())
		return;

	// THE TERMINATOR IS INCLUDED. The macOS layer builds the pasteboard
	// item with stringWithUTF8String, which reads to a NUL and does not
	// take a length - a buffer without one would read off the end of the
	// allocation and put whatever followed it on the pasteboard.
	auto package = CDropSource::create (payload.c_str (),
	                                    static_cast<uint32_t> (payload.size () + 1),
	                                    IDataPackage::kText);
	if (!package)
		return;

	// No drag bitmap: sixty-four pads all look alike at this size, and
	// the highlight under the pointer already says where the drop will
	// land. The cursor's own copy badge says which operation it will be.
	doDrag (DragDescription (package));
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

		// THE TYPE IS NOT TRUSTED, only the content. A file dragged on
		// macOS comes back reported as kText and holding a percent-escaped
		// file:// URL, because VSTGUI's unpacker asks the pasteboard item
		// for a string before it asks for a file URL and an item written
		// from an NSURL offers both - so a reader that insisted on
		// kFilePath would refuse every drag there is. kBinary is skipped
		// because it has no text in it to read.
		if (type == IDataPackage::kError || type == IDataPackage::kBinary
		    || buffer == nullptr || size == 0)
			continue;

		// UTF-8, and the size MAY OR MAY NOT include the terminator
		// depending on the platform layer that packed it. Taking the
		// length with strnlen handles both; constructing from `size`
		// alone would leave a trailing NUL inside the string, which then
		// compares unequal to the same path read back out of a project
		// file and quietly breaks every comparison downstream.
		const char* chars = static_cast<const char*> (buffer);
		const std::string text (chars, ::strnlen (chars, size));

		// One question, asked in Project6Slots.cpp where it can be
		// tested: plain path, file:// URL, several lines, or none of the
		// above.
		const std::string path = sampleFilePathFromDragText (text);
		if (!path.empty ())
			return path;
	}

	return std::string ();
}

//------------------------------------------------------------------------
bool SpySampleSlot::slotDrag (IDataPackage* package, int& from, std::string& path)
{
	if (package == nullptr)
		return false;

	const uint32_t count = package->getCount ();
	for (uint32_t i = 0; i < count; ++i)
	{
		const void* buffer = nullptr;
		IDataPackage::Type type = IDataPackage::kError;
		const uint32_t size = package->getData (i, buffer, type);

		if (type == IDataPackage::kError || buffer == nullptr || size == 0)
			continue;

		const char* chars = static_cast<const char*> (buffer);
		const std::string text (chars, ::strnlen (chars, size));

		if (decodeSlotDrag (text, &from, &path))
			return true;
	}

	return false;
}

//------------------------------------------------------------------------
bool SpySampleSlot::copyRequested (const Modifiers& modifiers)
{
	return modifiers.has (ModifierKey::Control) || modifiers.has (ModifierKey::Alt);
}

//------------------------------------------------------------------------
DragOperation SpySampleSlot::onDragEnter (DragEventData data)
{
	int from = -1;
	std::string path;

	if (slotDrag (data.drag, from, path))
	{
		// A PAD DROPPED ON ITSELF IS NOT A DROP. Lighting up for it would
		// promise something, and nothing is what should happen.
		mDragOver = (from != mIndex);
	}
	else
	{
		// The acceptance test runs HERE, not only on the drop, so the
		// pointer says no before the mouse button is released. A slot that
		// lights up for an .mp3 and then silently ignores it is worse than
		// one that never lit up.
		mDragOver = ! firstAcceptedPath (data.drag).empty ();
	}

	invalid ();
	return onDragMove (data);
}

//------------------------------------------------------------------------
DragOperation SpySampleSlot::onDragMove (DragEventData data)
{
	// WHETHER it will be taken cannot change while the pointer is inside
	// one slot - the package is the same package, and re-scanning it
	// hundreds of times a second would cost for nothing. WHICH OPERATION
	// it is can change, though: the modifier is read live, so letting go
	// of Control mid-drag turns the copy back into a move and the cursor
	// says so before the button comes up.
	if (! mDragOver)
		return DragOperation::None;

	// A file arriving from outside is always a copy - it stays where it
	// is on disk. Only a pad moving to another pad can be a move.
	int from = -1;
	std::string path;
	if (! slotDrag (data.drag, from, path))
		return DragOperation::Copy;

	return copyRequested (data.modifiers) ? DragOperation::Copy : DragOperation::Move;
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

	// ANOTHER PAD, first: its payload is this plug-in's own shape and
	// cannot be mistaken for a file, so there is nothing to disambiguate.
	int from = -1;
	std::string dragged;
	if (slotDrag (data.drag, from, dragged))
	{
		if (from == mIndex || ! isSlotIndex (from))
			return false;

		// THE MODIFIER IS READ HERE, at the drop, and not remembered from
		// when the drag began - which is the platform's own rule, and
		// means someone can change their mind half way across the grid.
		if (mMoveHandler)
			mMoveHandler (from, mIndex, copyRequested (data.modifiers));

		return true;
	}

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
	// THE OVERLAY WINS. While the level bar below is being dragged this
	// is the level in decibels, and it is what the reader wants to see -
	// the filename has not changed and will still be there afterwards.
	if (!mOverlay.empty ())
	{
		context->setFont (panelFont ());
		const CCoord height = panelFont ()->getSize () + 2.;
		const CRect line (band.left,
		                  band.top + (band.getHeight () - height) * 0.5,
		                  band.right,
		                  band.top + (band.getHeight () - height) * 0.5 + height);
		context->setFontColor (kSlotText);
		context->drawString (mOverlay.c_str (), line, kCenterText, true);
		return;
	}

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
	// A MIDI PAD IS A DIFFERENT COLOUR, but only in the resting state:
	// live is live and waiting is waiting whatever is in the slot, and
	// three more colours to say the same two things would be worse than
	// one colour saying one thing.
	const CColor resting = mPath.empty ()
		? kWellFill
		: (mKind == SlotFileKind::Midi ? kWellFillMidi : kWellFillFull);

	context->setFillColor (sounding ? kWellFillLive
	                                : (waiting ? kWellFillArmed : resting));
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

	// THE PLAYHEAD, along the bottom of the well, and only while the pad
	// is actually sounding. Drawn from `sounding` and not from the
	// trigger, so it can never contradict the lit well above it - an
	// armed pad waiting for its bar line shows no bar, because nothing is
	// playing yet.
	if (sounding)
	{
		CRect track (r);
		track.inset (1., 1.);
		track.top = track.bottom - kProgressHeight;

		if (track.getWidth () > 0. && track.getHeight () > 0.)
		{
			// The unplayed remainder is left as the well, so the bar is
			// one mark rather than two - and a pad at the very start of
			// its file shows a sliver rather than nothing, which is the
			// difference between "just started" and "not running".
			CRect played (track);
			played.right = played.left
			               + std::max (1., played.getWidth ()
			                                   * static_cast<double> (mProgress));

			context->setFillColor (kProgressBar);
			context->drawRect (played, kDrawFilled);
		}
	}

	setDirty (false);
}

//------------------------------------------------------------------------
// SpySlotLevel
//------------------------------------------------------------------------

namespace {

/** The DXi's own slider law: one unit of a 0..100 range per pixel of
    horizontal movement, in either direction. */
constexpr float kUnitsPerPixel = 0.01f;

} // namespace

//------------------------------------------------------------------------
SpySlotLevel::SpySlotLevel (const CRect& size, IControlListener* listener,
                            int32_t tag, int index)
: CControl (size, listener, tag)
, mIndex (index)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);
}

//------------------------------------------------------------------------
void SpySlotLevel::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	// The ground, then the fill to the current value, then the raised
	// edge - the order SpySlider draws its bar in, and the same colours.
	context->setFillColor (Colours::kLampOff);
	context->drawRect (r, kDrawFilled);

	CRect fill (r);
	fill.inset (1., 1.);
	fill.right = fill.left
	             + fill.getWidth () * std::clamp (
	                   static_cast<double> (getValueNormalized ()), 0.0, 1.0);
	if (fill.getWidth () > 0. && fill.getHeight () > 0.)
	{
		// DIMMED ON A MIDI PAD, where there is no audio for it to scale.
		// The bar is still real, still saved and still works the moment
		// an audio file lands here - what it must not do is look live
		// while doing nothing.
		context->setFillColor (mApplies ? Colours::kBarFill : kSlotTextIdle);
		context->drawRect (fill, kDrawFilled);
	}

	drawWell (context, r, Colours::kBarLight, Colours::kBarHigh);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpySlotLevel::setApplies (bool applies)
{
	if (applies == mApplies)
		return;

	mApplies = applies;
	invalid ();
}

//------------------------------------------------------------------------
void SpySlotLevel::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// NO ABSOLUTE POSITIONING, as in SpySlider: the value moves by
	// increments from wherever it was. On a bar this size, jumping to
	// the pointer would make every setting a coarse one.
	mDragging = true;
	mLastPoint = event.mousePosition;
	beginEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpySlotLevel::onMouseMoveEvent (MouseMoveEvent& event)
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

//------------------------------------------------------------------------
void SpySlotLevel::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;

	mDragging = false;
	endEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpySlotLevel::onMouseCancelEvent (MouseCancelEvent& event)
{
	// A cancelled drag still has to end its edit, or the host is left
	// holding a gesture open for ever.
	if (! mDragging)
		return;

	mDragging = false;
	endEdit ();
}

//------------------------------------------------------------------------
void SpySlotLevel::onMouseWheelEvent (MouseWheelEvent& event)
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
// SpySlotTranspose
//------------------------------------------------------------------------

namespace {

/** How far the pointer travels for one semitone. Three pixels puts the
    whole two-octave range in a hundred and forty-four pixels of travel -
    about four times the control's own width, which is the usual ratio for
    a small control and is close enough that a drag feels attached to the
    thing it is dragging. */
constexpr double kPixelsPerSemitone = 3.;

} // namespace

SpySlotTranspose::SpySlotTranspose (const CRect& size, IControlListener* listener,
                                    int32_t tag, int index)
: CControl (size, listener, tag)
, mIndex (index)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);
}

//------------------------------------------------------------------------
int SpySlotTranspose::semitones () const
{
	// THROUGH THE PARAMETER'S OWN DEFINITION, not through arithmetic
	// written out a second time here. toInternal is what the processor
	// calls with the identical normalised value, so the number on the
	// panel is the number the notes are moved by - which is the house
	// rule about one shared function, applied to the smallest control on
	// the panel because that is exactly where a second opinion would go
	// unnoticed longest.
	return static_cast<int> (slotTransposeDef ().toInternal (
		std::clamp (static_cast<double> (getValueNormalized ()), 0.0, 1.0)));
}

//------------------------------------------------------------------------
void SpySlotTranspose::setSemitones (int value)
{
	const ParamDef& def = slotTransposeDef ();
	const int clamped = std::clamp (value,
	                                static_cast<int> (def.plainMin),
	                                static_cast<int> (def.plainMax));

	setValueNormalized (static_cast<float> (
		std::clamp (def.toNormalized (static_cast<double> (clamped)), 0.0, 1.0)));
}

//------------------------------------------------------------------------
void SpySlotTranspose::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	const int value = semitones ();

	context->setFillColor (Colours::kLampOff);
	context->drawRect (r, kDrawFilled);

	CRect inner (r);
	inner.inset (1., 1.);

	// THE MIDDLE OF THE INNER RECT, to a whole pixel. A centre that
	// landed on a half would make +1 and -1 draw different widths, and on
	// a bar this small that reads as the control being wrong rather than
	// as rounding.
	const CCoord centre = std::floor (inner.left + inner.getWidth () * 0.5);

	if (value != 0 && inner.getHeight () > 0.)
	{
		const ParamDef& def = slotTransposeDef ();
		const double reach = inner.getWidth () * 0.5
		                     * (std::fabs (static_cast<double> (value)) / def.plainMax);

		// AT LEAST ONE PIXEL. A single semitone out of twenty-four is
		// less than a pixel of an eighteen-pixel half-width, and a bar
		// that drew nothing would say "not transposed" about a pad that
		// is.
		const CCoord width = std::max (1., reach);

		CRect fill (inner);
		fill.left  = (value > 0) ? centre : centre - width;
		fill.right = (value > 0) ? centre + width : centre;

		context->setFillColor (Colours::kBarFill);
		context->drawRect (fill, kDrawFilled);
	}

	// THE NUMBER, over the bar. The bar says which way and roughly how
	// far at a glance; the number is what somebody actually reads when
	// they want to know. Signed, because "12" and "-12" are two octaves
	// apart and an unsigned one would be a trap.
	char text[8] = {};
	if (value > 0)
		std::snprintf (text, sizeof (text), "+%d", value);
	else
		std::snprintf (text, sizeof (text), "%d", value);

	context->setFont (panelFontTiny ());
	context->setFontColor (value == 0 ? kSlotTextIdle : kSlotText);

	const CCoord height = panelFontTiny ()->getSize () + 2.;
	const CRect line (r.left,
	                  r.top + (r.getHeight () - height) * 0.5,
	                  r.right,
	                  r.top + (r.getHeight () - height) * 0.5 + height);
	context->drawString (text, line, kCenterText, true);

	drawWell (context, r, Colours::kBarLight, Colours::kBarHigh);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpySlotTranspose::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	event.consumed = true;

	// BACK TO ITS OWN PITCH. The one value somebody will want in a hurry,
	// and the one a three-pixels-per-semitone drag is least likely to
	// land on by hand.
	if (event.clickCount > 1)
	{
		beginEdit ();
		setSemitones (0);
		valueChanged ();
		endEdit ();
		invalid ();
		return;
	}

	mDragging      = true;
	mPressPoint    = event.mousePosition;
	mPressSemitones = semitones ();
	beginEdit ();
}

//------------------------------------------------------------------------
void SpySlotTranspose::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mDragging)
		return;

	// FROM THE PRESS, not from the last move. Accumulating per move
	// throws away the sub-semitone remainder on every call, so a slow
	// drag moves less far than a fast one over the same distance - which
	// is the kind of thing that feels broken without ever being wrong
	// enough to report.
	const double dx = event.mousePosition.x - mPressPoint.x;
	const double scale = event.modifiers.has (ModifierKey::Shift) ? 3. : 1.;
	const double steps = dx / (kPixelsPerSemitone * scale);

	// Away from zero, so the first semitone is reached in one direction
	// as easily as the other.
	const int moved = static_cast<int> (steps < 0. ? steps - 0.5 : steps + 0.5);
	const int wanted = mPressSemitones + moved;

	if (wanted == semitones ())
	{
		event.consumed = true;
		return;
	}

	setSemitones (wanted);
	valueChanged ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpySlotTranspose::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;

	mDragging = false;
	endEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpySlotTranspose::onMouseCancelEvent (MouseCancelEvent&)
{
	// A cancelled drag still has to end its edit, or the host is left
	// holding a gesture open for ever.
	if (! mDragging)
		return;

	mDragging = false;
	endEdit ();
}

//------------------------------------------------------------------------
void SpySlotTranspose::onMouseWheelEvent (MouseWheelEvent& event)
{
	event.consumed = true;
	if (event.deltaY == 0.)
		return;

	// ONE SEMITONE PER CLICK, and twelve with shift: the two intervals
	// anybody reaches for. A percentage step here would be unusable -
	// every value is a real interval and there are only forty-nine.
	const int step = event.modifiers.has (ModifierKey::Shift) ? 12 : 1;

	beginEdit ();
	setSemitones (semitones () + (event.deltaY > 0. ? step : -step));
	valueChanged ();
	endEdit ();
	invalid ();
}

//------------------------------------------------------------------------
// SpySlotDivision
//------------------------------------------------------------------------

SpySlotDivision::SpySlotDivision (const CRect& size, IControlListener* listener,
                                  int32_t tag, int index)
: CControl (size, listener, tag)
, mIndex (index)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);

	// A fixed tooltip: the VALUE is on the face of the control, so the
	// tip only has to say what the control is and how to work it.
	setTooltipText ("Launch quantise — click to step, right-click to step back");
}

//------------------------------------------------------------------------
LaunchDivision SpySlotDivision::current () const
{
	// Through the parameter's own definition, so the control and the host
	// agree about which normalised value is which division.
	return divisionFromIndex (static_cast<int> (
		slotDivisionDef ().toInternal (getValueNormalized ())));
}

//------------------------------------------------------------------------
void SpySlotDivision::step (int delta)
{
	const int next = ((indexOfDivision (current ()) + delta) % kLaunchDivisionCount
	                  + kLaunchDivisionCount) % kLaunchDivisionCount;

	// A COMPLETE GESTURE, like every other control here: begin, set,
	// notify, end - so a host can record and undo it.
	beginEdit ();
	setValueNormalized (static_cast<float> (
		slotDivisionDef ().toNormalized (static_cast<double> (next))));
	valueChanged ();
	endEdit ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySlotDivision::onMouseDownEvent (MouseDownEvent& event)
{
	// Ctrl counts as a right click, which is the macOS convention and a
	// fallback for hosts that keep the right button to themselves - the
	// same rule SpySelector follows.
	const bool back = event.buttonState.isRight ()
	                  || event.modifiers.has (ModifierKey::Control);

	if (! event.buttonState.isLeft () && ! back)
		return;

	event.consumed = true;
	step (back ? -1 : 1);
}

//------------------------------------------------------------------------
void SpySlotDivision::onMouseWheelEvent (MouseWheelEvent& event)
{
	event.consumed = true;
	if (event.deltaY != 0.)
		step (event.deltaY > 0. ? 1 : -1);
}

//------------------------------------------------------------------------
void SpySlotDivision::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	// A RAISED box, like the column buttons: it is something to press,
	// not a well to drop into.
	context->setFillColor (kWellFillFull);
	context->drawRect (r, kDrawFilled);
	drawWell (context, r, kWellHigh, kWellShadow);

	// The short name - "1/4" - because that is all there is room for and
	// all a reader needs. panelFontTiny is nine points; the box is
	// thirteen pixels tall.
	context->setFont (panelFontTiny ());
	context->setFontColor (kSlotText);

	const CCoord height = panelFontTiny ()->getSize () + 2.;
	const CRect line (r.left,
	                  r.top + (r.getHeight () - height) * 0.5,
	                  r.right,
	                  r.top + (r.getHeight () - height) * 0.5 + height);

	context->drawString (divisionShortName (current ()), line, kCenterText, true);

	setDirty (false);
}

//------------------------------------------------------------------------
// SpySlotFit
//------------------------------------------------------------------------

SpySlotFit::SpySlotFit (const CRect& size, IControlListener* listener,
                        int32_t tag, int index)
: CControl (size, listener, tag)
, mIndex (index)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);
	refreshTooltip ();
}

//------------------------------------------------------------------------
FitMode SpySlotFit::current () const
{
	// Through the parameter's own definition, like every other control
	// here, so the box and the host agree about which value is which.
	return fitModeFromIndex (static_cast<int> (
		slotFitDef ().toInternal (getValueNormalized ())));
}

//------------------------------------------------------------------------
void SpySlotFit::step (int delta)
{
	const int next = ((indexOfFitMode (current ()) + delta) % kFitModeCount
	                  + kFitModeCount) % kFitModeCount;

	beginEdit ();
	setValueNormalized (static_cast<float> (
		slotFitDef ().toNormalized (static_cast<double> (next))));
	valueChanged ();
	endEdit ();
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySlotFit::setFitted (bool fitted)
{
	if (fitted == mFitted)
		return;

	mFitted = fitted;
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySlotFit::setTempoText (const std::string& text)
{
	if (text == mTempoText)
		return;

	mTempoText = text;
	refreshTooltip ();
}

//------------------------------------------------------------------------
void SpySlotFit::refreshTooltip ()
{
	std::string text = "Tempo fit — click to step, right-click to step back";
	if (!mTempoText.empty ())
	{
		text += "\n";
		text += mTempoText;
	}
	setTooltipText (text.c_str ());
}

//------------------------------------------------------------------------
void SpySlotFit::onMouseDownEvent (MouseDownEvent& event)
{
	const bool back = event.buttonState.isRight ()
	                  || event.modifiers.has (ModifierKey::Control);

	if (! event.buttonState.isLeft () && ! back)
		return;

	event.consumed = true;
	step (back ? -1 : 1);
}

//------------------------------------------------------------------------
void SpySlotFit::onMouseWheelEvent (MouseWheelEvent& event)
{
	event.consumed = true;
	if (event.deltaY != 0.)
		step (event.deltaY > 0. ? 1 : -1);
}

//------------------------------------------------------------------------
void SpySlotFit::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	context->setFillColor (kWellFillFull);
	context->drawRect (r, kDrawFilled);
	drawWell (context, r, kWellHigh, kWellShadow);

	context->setFont (panelFontTiny ());

	// DIMMED WHEN NOTHING IS BEING FITTED. The mode is still readable -
	// it is a setting, and it will apply the moment a file with a tempo
	// arrives - but a bright "spd" against a pad that is playing its file
	// untouched would be the panel telling a lie about the sound.
	context->setFontColor (mFitted ? kSlotText : kSlotTextIdle);

	const CCoord height = panelFontTiny ()->getSize () + 2.;
	const CRect line (r.left,
	                  r.top + (r.getHeight () - height) * 0.5,
	                  r.right,
	                  r.top + (r.getHeight () - height) * 0.5 + height);

	context->drawString (fitModeShortName (current ()), line, kCenterText, true);

	setDirty (false);
}

//------------------------------------------------------------------------
// SpySlotLoop
//------------------------------------------------------------------------

SpySlotLoop::SpySlotLoop (const CRect& size, IControlListener* listener,
                          int32_t tag, int index)
: CControl (size, listener, tag)
, mIndex (index)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);

	setTooltipText ("Loop or one-shot — L loops, 1 plays once and stops itself");
}

//------------------------------------------------------------------------
void SpySlotLoop::toggle ()
{
	beginEdit ();
	setValueNormalized (looping () ? 0.f : 1.f);
	valueChanged ();
	endEdit ();
	invalid ();
}

//------------------------------------------------------------------------
void SpySlotLoop::onMouseDownEvent (MouseDownEvent& event)
{
	// EITHER BUTTON DOES THE SAME THING. The two boxes beside this one
	// step forwards on a left click and backwards on a right one, because
	// they have three and four choices; with two there is nowhere to go
	// except back, and a right-click that appeared to do something
	// different would be a lie about a control this small.
	const bool any = event.buttonState.isLeft () || event.buttonState.isRight ()
	                 || event.modifiers.has (ModifierKey::Control);
	if (! any)
		return;

	event.consumed = true;
	toggle ();
}

//------------------------------------------------------------------------
void SpySlotLoop::onMouseWheelEvent (MouseWheelEvent& event)
{
	event.consumed = true;
	if (event.deltaY != 0.)
		toggle ();
}

//------------------------------------------------------------------------
void SpySlotLoop::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	context->setFillColor (kWellFillFull);
	context->drawRect (r, kDrawFilled);
	drawWell (context, r, kWellHigh, kWellShadow);

	context->setFont (panelFontTiny ());

	// A ONE-SHOT IS THE ONE THAT IS MARKED. Loop is the default and
	// sixty-three pads out of sixty-four will be on it, so it is the
	// quiet state and the "1" is what the eye should find.
	context->setFontColor (looping () ? kSlotTextIdle : kSlotText);

	const CCoord height = panelFontTiny ()->getSize () + 2.;
	const CRect line (r.left,
	                  r.top + (r.getHeight () - height) * 0.5,
	                  r.right,
	                  r.top + (r.getHeight () - height) * 0.5 + height);

	context->drawString (looping () ? "L" : "1", line, kCenterText, true);

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
