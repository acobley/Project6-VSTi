//------------------------------------------------------------------------
// Project6 - editor implementation
//------------------------------------------------------------------------

#include "Project6Editor.h"
#include "Project6Controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace Project6 {

namespace {

/** The panel behind the controls. SpyBand's dialog background was a
    bitmap; this one is a colour, because there is no artwork to recover
    and inventing some would only be something else to keep in step with
    the layout. The value is SpyBand's own fallback - the colour its editor
    paints under the bitmap so a missing file reads as a dark panel rather
    than as whatever the host left in the window. */
const CColor kPanel (64, 64, 64, 255);

} // namespace

//------------------------------------------------------------------------
Project6Editor::Project6Editor (Project6Controller* controller)
: VSTGUIEditor (controller)
, mController (controller)
{
	ViewRect rect (0, 0, kEditorWidth, kEditorHeight);
	setRect (rect);
}

//------------------------------------------------------------------------
CRect Project6Editor::cell (int column, int row) const
{
	const CCoord x = kMargin + column * (kSliderWidth + kColumnGap);
	const CCoord y = kGridTop + row * (kSliderHeight + kRowGap);
	return CRect (x, y, x + kSliderWidth, y + kSliderHeight);
}

//------------------------------------------------------------------------
void Project6Editor::setParameter (ParamID tag, double plain)
{
	if (mController == nullptr)
		return;

	const ParamDef& def = paramDef (tag);
	const double normalized = std::min (1.0, std::max (0.0, def.toNormalized (plain)));

	// A COMPLETE GESTURE. beginEdit / endEdit around it is what makes a
	// host treat a write as something it can record and undo, rather than
	// as an unexplained jump; setParamNormalized is what moves this
	// panel's own control, because it comes back through updateControl.
	mController->beginEdit (tag);
	mController->setParamNormalized (tag, normalized);
	mController->performEdit (tag, normalized);
	mController->endEdit (tag);
}

//------------------------------------------------------------------------
SpySlider* Project6Editor::addSlider (ParamID tag, const char* label, const CRect& rect)
{
	auto* control = new SpySlider (rect, this, static_cast<int32_t> (tag));
	control->setLabel (label);

	// The number across the middle is the parameter's own PLAIN value,
	// read out of the same table the host formats from - so the panel and
	// the host cannot disagree about what a control says. Hertz get no
	// decimal place; decibels and per cent get one, because a dB is worth
	// resolving and 100 % should not read as 99 %.
	const ParamDef& def = paramDef (tag);
	const bool integral = (def.units != nullptr && def.units[0] == 'H');
	control->setFormatter ([def, integral] (float normalized)
	{
		char buffer[32];
		std::snprintf (buffer, sizeof (buffer), integral ? "%.0f %s" : "%.1f %s",
		               def.toPlain (normalized), def.units ? def.units : "");
		return std::string (buffer);
	});

	mControls[tag] = control;
	if (mController)
		showValue (control, mController->getParamNormalized (tag));

	// Z-order is the order views are added, and every control here is a
	// direct child of the frame - so a control's getViewSize() is already
	// in frame coordinates and nothing needs a parent chain walked.
	frame->addView (control);
	return control;
}

//------------------------------------------------------------------------
CTextLabel* Project6Editor::addHeading (const char* text, const CRect& rect)
{
	auto* label = new CTextLabel (rect);
	label->setFont (panelFont ());
	label->setFontColor (Colours::kLabel);
	label->setBackColor (kTransparentCColor);
	label->setFrameColor (kTransparentCColor);
	label->setStyle (CParamDisplay::kNoFrame);
	label->setText (text);
	frame->addView (label);
	return label;
}

//------------------------------------------------------------------------
bool PLUGIN_API Project6Editor::open (void* parent, const PlatformType& platformType)
{
	if (frame != nullptr)
		return false;

	const CRect frameSize (0, 0, kEditorWidth, kEditorHeight);
	frame = new CFrame (frameSize, this);
	frame->setBackgroundColor (kPanel);

	//--------------------------------------------------------------------
	// The title.
	//--------------------------------------------------------------------
	addHeading ("Project6  -  instrument",
	            CRect (kMargin, kTitleTop, kEditorWidth - kMargin,
	                   kTitleTop + kTitleHeight));

	//--------------------------------------------------------------------
	// The heading over the control column.
	//--------------------------------------------------------------------
	{
		const CRect head = cell (0, 0);
		addHeading ("Output",
		            CRect (head.left, kHeadingTop, head.right,
		                   kHeadingTop + kHeadingHeight));
	}

	//--------------------------------------------------------------------
	// The controls. A TABLE, not a sequence of hand-placed calls: adding
	// the next one is a row here and a bump to kRows / kColumns in the
	// header, and nothing else on the panel moves.
	//--------------------------------------------------------------------
	{
		static const struct { ParamID tag; const char* label; int column, row; } kGrid[] =
		{
			{ kOutputTrim, "Output Trim", 0, 0 },
		};

		for (const auto& entry : kGrid)
			addSlider (entry.tag, entry.label, cell (entry.column, entry.row));
	}

	//--------------------------------------------------------------------
	// The display, filling the space to the right.
	//--------------------------------------------------------------------
	mDisplay = new SpyDisplay (
		CRect (kDisplayLeft, kDisplayTop,
		       kDisplayLeft + kDisplayWidth, kDisplayBottom));
	frame->addView (mDisplay);

	refreshDisplay ();

	// The display follows the DSP, which can move without anything on the
	// panel being touched - a host automating the trim, or a smoother
	// still travelling - so it is PULLED on a timer rather than pushed by
	// a control.
	mTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { refreshDisplay (); },
	                                  kTimerMs, true);

	frame->open (parent, platformType);
	return true;
}

//------------------------------------------------------------------------
void Project6Editor::refreshDisplay ()
{
	if (mDisplay == nullptr || mController == nullptr)
		return;

	mDisplay->setLevel (plainOf (kOutputTrim), mController->dspSampleRate ());
}

//------------------------------------------------------------------------
void Project6Editor::showValue (CControl* control, double normalized)
{
	if (control == nullptr)
		return;
	control->setValueNormalized (static_cast<float> (normalized));
	control->invalid ();
}

//------------------------------------------------------------------------
double Project6Editor::plainOf (ParamID tag) const
{
	if (mController == nullptr)
		return 0.0;
	return paramDef (tag).toPlain (mController->getParamNormalized (tag));
}

//------------------------------------------------------------------------
void PLUGIN_API Project6Editor::close ()
{
	mTimer = nullptr;

	mControls.clear ();
	mDisplay = nullptr;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
void Project6Editor::valueChanged (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	const ParamValue value = control->getValueNormalized ();

	mController->setParamNormalized (tag, value);
	mController->performEdit (tag, value);
}

//------------------------------------------------------------------------
void Project6Editor::controlBeginEdit (CControl* control)
{
	if (mController && control)
		mController->beginEdit (static_cast<ParamID> (control->getTag ()));
}

//------------------------------------------------------------------------
void Project6Editor::controlEndEdit (CControl* control)
{
	if (mController && control)
		mController->endEdit (static_cast<ParamID> (control->getTag ()));
}

//------------------------------------------------------------------------
void Project6Editor::updateControl (ParamID tag, ParamValue normalized)
{
	if (frame == nullptr)
		return;

	auto it = mControls.find (tag);
	if (it == mControls.end () || it->second == nullptr)
		return;

	showValue (it->second, normalized);

	// The display reads parameters rather than controls, so it has to be
	// told too - the timer would get there within 30 ms, but a control
	// moved by the host should not visibly lag the panel it moved.
	refreshDisplay ();
}

//------------------------------------------------------------------------
} // namespace Project6
