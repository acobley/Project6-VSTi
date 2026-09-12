//------------------------------------------------------------------------
// Project6 - the grid's own controls
//
// Four of them: SpySampleSlot, one cell of the 8 x 8 bank; SpySlotLevel,
// the level bar under it; SpySlotDivision, the box beside that bar which
// says which grid line the pad waits for; and SpyColumnButton, above each
// column, which launches all of it. They are together because they are
// views of the same grid and none means anything without the others.
//
// One cell of the 8 x 8 grid. Drag a .wav onto it from the Finder and it
// takes the file; click it and the file loops from the next bar line;
// click it again and it stops at the one after that. Empty, it is blank -
// just the well.
//
// A LOADED PAD CAN ALSO BE PICKED UP and dropped on another pad, which
// MOVES it - the two cells exchange, so a mis-aimed drag never destroys a
// slot - or COPIES it with Control or Alt held. The whole cell travels,
// level and launch division and tempo fit together, because a pad is what
// you set up and dragging it should rearrange the bank rather than the
// filenames. Both pads are stopped on the way, since a voice running
// through a buffer that changed underneath it is a click at best.
//
// THE CLICK AND THE SOUND ARE NOW TWO DIFFERENT THINGS, and the slot has
// to show both. What was asked for is the trigger parameter - this
// control's own value. What is happening is published back by the
// processor and arrives through setSounding(). While they disagree the
// slot is WAITING FOR A BAR LINE, in whichever direction, and says so
// with an amber edge; a pad clicked into silence with no visible change
// would be the worst kind of control this panel could have.
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
#include "Project6Stretch.h"
#include "Project6Transport.h"

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

	/** Show this INSTEAD of the filename, until it is set back to empty.

	    It exists for the level bar underneath, which is nine pixels tall
	    and has nowhere to print a number. While the bar is being dragged
	    the pad above it reads out the level in decibels and then goes
	    back to being a filename - the same trick VocalFilter's SpySlider
	    plays with its own value text, using space that is already there
	    rather than adding a readout to sixty-four cells. */
	void setOverlay (const std::string& text);

	/** How that file actually read, as the processor reported it. A slot
	    that will not play has to be able to SAY so - a slot that takes
	    the drop, shows the name and does nothing when clicked is the
	    worst thing a control can do. */
	void setStatus (SampleStatus status);

	/** What was read out of the file about its TEMPO - "100.0 BPM (ACID
	    chunk)" - appended to the pad's tooltip under the path.

	    On the pad as well as on the fit box because the pad is the big
	    target: someone wondering why a loop sounds wrong hovers the name,
	    not the twenty-six-pixel box beside it. */
	void setTempoText (const std::string& text);

	/** WHICH KIND OF FILE this pad is holding, so it can be drawn as what
	    it is. A MIDI pad takes a different well colour - the well is the
	    biggest thing on a cell and colour is what the eye finds across
	    sixty-four of them, where a badge or a letter would be one more
	    small thing to read. */
	void setKind (SlotFileKind kind);
	SlotFileKind kind () const { return mKind; }

	SampleStatus status () const { return mStatus; }

	/** True when there is a loaded file here and a click should start it. */
	bool playable () const
	{
		return !mPath.empty () && mStatus == SampleStatus::Loaded;
	}

	/** Whether this slot is ACTUALLY making a sound, as published by the
	    processor. Not the same as its parameter: between a click and the
	    next bar line the two deliberately disagree. */
	void setSounding (bool sounding);
	bool sounding () const { return mSounding; }

	/** How far through its file the pad is, 0 to 1 - drawn as a bar along
	    the bottom of the well while it plays. */
	void setProgress (float progress);

	/** What the user has asked for - this control's own parameter. */
	bool armed () const { return getValueNormalized () >= 0.5f; }

	/** Armed and not yet sounding, or sounding and no longer armed:
	    either way, waiting for a bar line. Public because the column
	    button above this slot summarises it. */
	bool pending () const { return armed () != mSounding; }

	/** Called with (index, path) when a file the plug-in will take is
	    dropped here. */
	void setHandler (std::function<void (int, const std::string&)> handler);

	/** Called with (from, to, copy) when ANOTHER PAD is dropped here.

	    A separate handler rather than a flag on the one above, because it
	    is a different operation with different consequences: the one
	    above fills a slot, this one rearranges the bank and can empty a
	    slot the user was not pointing at. */
	void setMoveHandler (std::function<void (int, int, bool)> handler);

	void draw (VSTGUI::CDrawContext* context) override;

	//--------------------------------------------------------------------
	// A CLICK AND A DRAG START THE SAME WAY, so the pad cannot decide
	// which it is until the button comes up or the pointer moves.
	//
	// This used to toggle the loop on mouse DOWN, deliberately - "a pad
	// should sound the instant it is hit", the way SpyToggle does it.
	// Picking a pad up to move its file has taken that away: a press that
	// launched immediately and then turned into a drag would have already
	// started a loop the user was only trying to move.
	//
	// So the launch happens on RELEASE, and only when the pointer did not
	// travel more than kDragThreshold. Nothing is audibly later for it -
	// a pad does not sound when it is clicked in any case, it sounds on
	// the next grid line, and the few milliseconds between a press and
	// its release are nothing beside a bar.
	//--------------------------------------------------------------------

	/** How far the pointer may travel before a press stops being a click.
	    Four pixels: far enough that a hand resting on a mouse still
	    clicks, short enough that a deliberate move is a move at once. */
	static constexpr VSTGUI::CCoord kDragThreshold = 4.;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;

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

	/** Is this drag another pad rather than a file? Fills `from` and
	    `path` and returns true when it is. See Project6Slots.h for why
	    the payload is text and what the alternative cost. */
	static bool slotDrag (VSTGUI::IDataPackage* package, int& from, std::string& path);

	/** Held down, this drop copies instead of moving.

	    CONTROL, as asked for - and Alt as well, because macOS turns a
	    Control-click into a right-click before the view ever sees it, and
	    a copy that could not be asked for on the platform this ships on
	    would be a feature that is not there. Alt is that platform's own
	    modifier for the same idea. */
	static bool copyRequested (const VSTGUI::Modifiers& modifiers);

	/** Put this pad on the pasteboard. Called once, when a press has
	    travelled far enough to be a drag. */
	void beginSlotDrag ();

	/** Draw the name into `band`, shortened until it fits: first by
	    shedding the .wav - every file here has one, so it is the four
	    characters least worth reading - then by dropping a font size, and
	    only then by cutting characters off the end. */
	void drawName (VSTGUI::CDrawContext* context, const VSTGUI::CRect& band);

	/** Path and, when there is something to explain, why it will not
	    play. */
	void refreshTooltip ();


	int mIndex = 0;
	std::string mPath;
	std::string mOverlay;
	std::string mTempoText;
	SampleStatus mStatus = SampleStatus::Empty;
	SlotFileKind mKind = SlotFileKind::None;
	bool mSounding = false;

	/** The press that has not yet decided whether it is a click or a
	    drag, and where it started. */
	bool mPressed = false;
	VSTGUI::CPoint mPressPoint;
	/** Set once a press has become a drag, so the release does not also
	    launch the pad. */
	bool mDragStarted = false;
	float mProgress = 0.f;
	std::function<void (int, const std::string&)> mHandler;
	std::function<void (int, int, bool)> mMoveHandler;
	bool mDragOver = false;
};

//------------------------------------------------------------------------
/** The level bar under one pad.

    A SlideSpin's bar with everything else taken off: no label, no value
    text, no lamp - nine pixels of the DXi's own Draw3dRect and fill, and
    the same drag law, which is RELATIVE and one unit of a hundred per
    pixel. Absolute positioning on a bar this size would make every
    setting a coarse one, and it is what the original did not do either.

    It is a proper CControl with its own parameter tag, so a host can
    automate a slot's level exactly as it can automate its trigger. While
    it is dragged it asks the pad above to show the value, because there
    is no room for a number here. */
class SpySlotLevel : public VSTGUI::CControl
{
public:
	SpySlotLevel (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	              int32_t tag, int index);

	int index () const { return mIndex; }

	/** False on a MIDI pad, where there is no audio for a level to
	    scale.

	    THE BAR IS ALSO HIDDEN THERE, by the editor, because
	    SpySlotTranspose now occupies this same rectangle on a MIDI pad -
	    the strip keeps its shape, but what fills it depends on what the
	    pad holds. The dimming stays for the moment between the two and
	    for any future state where the bar is shown without applying: a
	    control must never look live while doing nothing, which is the
	    failure this project keeps coming back to.

	    The parameter itself is untouched either way - real, saved with
	    the project, and doing something again the moment an audio file is
	    dropped here. */
	void setApplies (bool applies);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlotLevel, VSTGUI::CControl)

private:
	int mIndex = 0;
	bool mApplies = true;
	bool mDragging = false;
	VSTGUI::CPoint mLastPoint;
};

//------------------------------------------------------------------------
/** The MIDI transpose control, IN THE LEVEL BAR'S PLACE.

    IT SHARES THE LEVEL BAR'S RECTANGLE and the two are never both shown:
    a pad holds a wav or a mid, the level bar is dead on a MIDI pad and
    this is dead on an audio one, so the strip under a cell stays exactly
    the shape it was and the panel does not grow by a pixel. That is the
    whole reason it lives here rather than in a fourth box: kSlotWidth is
    already spoken for three times over, and a hundred and twelve pixels
    times eight columns is the width of the window.

    CENTRE-ZERO, AND IT SAYS THE NUMBER. The bar fills from the middle
    outwards - right for up, left for down - so which way a pad has been
    moved is readable without counting, and the semitones are lettered
    over the top because "up a bit" is not something anybody wants to
    guess at. Zero draws no fill at all, which is what makes an untouched
    pad look untouched.

    DRAGGED IN SEMITONES, NOT IN PERCENT. Unlike the level bar, whose
    units are arbitrary and relative, every value here is a real musical
    interval and all forty-nine of them matter; the drag law is therefore
    pixels-per-semitone, so a drag lands on the semitone it looks like it
    landed on. A shift-drag is finer, a wheel click is one semitone and a
    double click returns the pad to its own pitch - which is the one
    value a person will want back in a hurry. */
class SpySlotTranspose : public VSTGUI::CControl
{
public:
	SpySlotTranspose (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	                  int32_t tag, int index);

	int index () const { return mIndex; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlotTranspose, VSTGUI::CControl)

private:
	/** The value as a person reads it: semitones, -24 to +24. The ONLY
	    place the normalised form is turned into semitones or back, so the
	    drawing and the dragging cannot come to different views about what
	    a step is. */
	int semitones () const;
	void setSemitones (int value);

	int mIndex = 0;
	bool mDragging = false;
	/** Where the drag started and what the value was there. ABSOLUTE from
	    the start of the drag rather than accumulated per move, so a drag
	    that wanders back to where it began ends on the value it began
	    on. */
	VSTGUI::CPoint mPressPoint;
	int mPressSemitones = 0;
};

//------------------------------------------------------------------------
/** The launch-division box, beside a pad's level bar.

    Four states - 1/1, 1/2, 1/4, 1/8 - and it says which of them the pad
    is on. A LEFT CLICK STEPS FORWARD and a right click steps back, which
    is the opposite way round from SpySelector: that control preserves a
    DXi's inverted convention deliberately, and this one has no DXi behind
    it, so it does the thing a person expects instead.

    A proper CControl with its own parameter tag, so a host can automate a
    pad's quantisation as it can automate everything else here. It shows
    the SHORT name; Project6Transport.h owns both names, so the panel's box
    and a host's own list cannot come to different views about what a
    division is called. */
class SpySlotDivision : public VSTGUI::CControl
{
public:
	SpySlotDivision (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	                 int32_t tag, int index);

	int index () const { return mIndex; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlotDivision, VSTGUI::CControl)

private:
	/** The division this control is currently showing. */
	LaunchDivision current () const;

	/** Write a new one as a complete edit gesture. `delta` wraps, so the
	    box cycles rather than sticking at either end - on a control this
	    small, a dead click is worse than a wrap. */
	void step (int delta);

	int mIndex = 0;
};

//------------------------------------------------------------------------
/** The box beside that one: what this pad does about the project's tempo.

    The same shape of control as SpySlotDivision - a small enumerated box
    that steps on a click - because it is the same kind of thing, and two
    boxes in one strip that behaved differently would be a trap.

    IT ALSO SAYS WHETHER IT IS DOING ANYTHING. A pad set to varispeed
    whose file has no detected tempo, or whose file is a one-shot, is not
    being fitted at all; the mode is set and nothing is happening. That is
    precisely the "a control that looks live and is not" failure this
    project keeps coming back to, so the label is DIMMED in that case and
    the tooltip says why. The editor decides - it is the side that knows
    the tempo - and hands the answer down through setFitted. */
class SpySlotFit : public VSTGUI::CControl
{
public:
	SpySlotFit (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	            int32_t tag, int index);

	int index () const { return mIndex; }

	/** True when this pad's file is actually being stretched or
	    resampled: a tempo was detected, the project has one, they differ,
	    and the mode is not Off. */
	void setFitted (bool fitted);

	/** What to say in the tooltip about this pad's file - "100 BPM (ACID
	    chunk)", or why nothing is being fitted. */
	void setTempoText (const std::string& text);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlotFit, VSTGUI::CControl)

private:
	FitMode current () const;
	void step (int delta);
	void refreshTooltip ();

	int  mIndex  = 0;
	bool mFitted = false;
	std::string mTempoText;
};

//------------------------------------------------------------------------
/** The smallest box in the strip: does this pad loop, or play once?

    THIRTEEN PIXELS AND ONE CHARACTER - an infinity-ish "L" for a loop, a
    "1" for a one-shot - because the strip already carries three controls
    and a fourth that needed three characters would have cost another
    twenty pixels on every cell and a hundred and sixty on the panel.

    A two-state control rather than an enumerated one: it steps on a click
    like the two boxes beside it, but there is nowhere to go except back,
    so a right-click does the same thing as a left one rather than
    pretending to go backwards through two states. */
class SpySlotLoop : public VSTGUI::CControl
{
public:
	SpySlotLoop (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	             int32_t tag, int index);

	int index () const { return mIndex; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlotLoop, VSTGUI::CControl)

private:
	bool looping () const { return getValueNormalized () >= 0.5f; }
	void toggle ();

	int mIndex = 0;
};

//------------------------------------------------------------------------
/** The box above a column: one press launches every loaded slot in it.

    It writes the slots' own trigger parameters and does nothing else, so
    the bar-line rules apply to a column press exactly as they apply to a
    click on one pad - eight pads come in together on the next bar line,
    which is the only way eight loops can start in time with each other.

    IT CARRIES NO PARAMETER OF ITS OWN, and that is the design rather than
    an omission. A column "state" would be a second opinion about the same
    eight triggers, and the moment somebody clicked one pad out of a
    launched column the two would disagree with nothing to say which was
    right. The precedent is VocalFilter's SpyPresetButton, which writes
    nine parameters and holds none: a host sees the writes and never sees
    the button.

    So it is a CView, not a CControl, and what it SHOWS is a summary the
    editor hands it - how many of its column can play, how many are
    sounding, and whether any of them is waiting for a bar line. */
class SpyColumnButton : public VSTGUI::CView
{
public:
	SpyColumnButton (const VSTGUI::CRect& size, int column);

	int column () const { return mColumn; }

	void setHandler (std::function<void (int)> handler);

	/** The state of the column below, recomputed by the editor whenever
	    anything in it moves. */
	void setState (int playable, int sounding, bool pending);

	void draw (VSTGUI::CDrawContext* context) override;

	/** Fires on mouse DOWN, like the pads - and for the same reason the
	    pads can: nothing happens until the next bar line, so the wait IS
	    the undo. A press you did not mean can be pressed again before it
	    takes effect. */
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;

	CLASS_METHODS (SpyColumnButton, VSTGUI::CView)

private:
	int mColumn = 0;
	int mPlayable = 0;
	int mSounding = 0;
	bool mPending = false;
	std::function<void (int)> mHandler;
};

//------------------------------------------------------------------------
} // namespace Project6
