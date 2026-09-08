//------------------------------------------------------------------------
// Project6 - the panel display, implementation
//------------------------------------------------------------------------

#include "Project6Display.h"
#include "Project6Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace Project6 {

namespace {

const CColor kPlate      (  0,   0,   0, 190);
const CColor kPlateEdge  (255, 255, 255, 110);
const CColor kGridLine   (255, 255, 255,  38);
const CColor kGridStrong (255, 255, 255,  70);
const CColor kCaption    (255, 255, 255, 200);

/** The output trace. White and heaviest, as the resultant is in every one
    of these panels - so when components are added underneath it in colour,
    this line still reads as the answer rather than as one more of them.

    NOT fully opaque, for the same reason it is not in VocalFilter: where
    the resultant sits exactly on top of a component - which is where it
    matters most - a solid line hides the colour completely and the colour
    key stops being true. */
const CColor kOutputTrace (255, 255, 255, 205);

/** Labelled horizontal lines. Every 12 dB, which puts one on unity and one
    on the bottom of the trim's travel. */
constexpr double kDbLines[] = { 12.0, 0.0, -12.0, -24.0, -36.0, -48.0, -60.0 };

/** The playhead: where the transport is through the bar. Bright, because
    it is the only thing on the panel that moves, and it is what says how
    long a clicked pad still has to wait. */
const CColor kPlayhead     (255, 190,  60, 255);
/** The same when the transport is stopped - still drawn, at the position
    the transport is parked at, but not competing for attention. */
const CColor kPlayheadIdle (255, 190,  60,  70);

/** Points across the plot. One per pixel would be wasteful on a wide panel
    and jagged on a narrow one; a fixed count keeps the curve smooth and
    the cost predictable. */
constexpr int kPoints = 240;

} // namespace

//------------------------------------------------------------------------
SpyDisplay::SpyDisplay (const CRect& size)
: CView (size)
{
	// Nothing here reads the mouse: it is a readout, not a control.
	setMouseEnabled (false);
}

//------------------------------------------------------------------------
bool SpyDisplay::setState (double trimDb, double sampleRate, int transport,
                           double phase, int beatsPerBar)
{
	const int beats = std::min (kMaxBeatsPerBar, std::max (1, beatsPerBar));

	const bool changed = (trimDb != mTrimDb) || (sampleRate != mSampleRate)
	                     || (transport != mTransport) || (phase != mBarPhase)
	                     || (beats != mBeatsPerBar);

	mTrimDb      = trimDb;
	mSampleRate  = sampleRate;
	mTransport   = transport;
	mBarPhase    = std::min (1.0, std::max (0.0, phase));
	mBeatsPerBar = beats;

	if (changed)
		invalid ();
	return changed;
}

//------------------------------------------------------------------------
CCoord SpyDisplay::xOf (double fraction, const CRect& plot) const
{
	return plot.left + plot.getWidth () * std::min (1.0, std::max (0.0, fraction));
}

CCoord SpyDisplay::yOf (double decibels, const CRect& plot) const
{
	const double t = (kMaxDb - decibels) / (kMaxDb - kMinDb);
	return plot.top + plot.getHeight () * std::min (1.0, std::max (0.0, t));
}

//------------------------------------------------------------------------
void SpyDisplay::drawGrid (CDrawContext* context, const CRect& plot)
{
	context->setLineWidth (1.);

	// ONE LINE PER BEAT, and the downbeat brighter - the plot is one bar
	// wide, so this is the ruler a person reads the playhead against. The
	// beat count is the host's own time signature numerator, published
	// for exactly this: a 3/4 bar ruled into four would be worse than no
	// ruling at all.
	for (int beat = 0; beat < mBeatsPerBar; ++beat)
	{
		context->setFrameColor ((beat == 0) ? kGridStrong : kGridLine);
		const CCoord x = xOf (beat / static_cast<double> (mBeatsPerBar), plot);
		context->drawLine (CPoint (x, plot.top), CPoint (x, plot.bottom));
	}

	for (double decibels : kDbLines)
	{
		// Unity is the line that matters, so it is the one drawn brightest.
		context->setFrameColor (decibels == 0.0 ? kGridStrong : kGridLine);
		const CCoord y = yOf (decibels, plot);
		context->drawLine (CPoint (plot.left, y), CPoint (plot.right, y));
	}
}

//------------------------------------------------------------------------
void SpyDisplay::drawPlayhead (CDrawContext* context, const CRect& plot)
{
	// Nothing to point at: the host reported no transport at all, so the
	// bar this plot draws is imaginary and a playhead on it would be a
	// claim rather than a readout.
	if (mTransport == kTransportUnknown)
		return;

	const CCoord x = xOf (mBarPhase, plot);

	context->setLineWidth (mTransport == kTransportPlaying ? 2. : 1.);
	context->setFrameColor (mTransport == kTransportPlaying ? kPlayhead : kPlayheadIdle);
	context->drawLine (CPoint (x, plot.top), CPoint (x, plot.bottom));
}

//------------------------------------------------------------------------
void SpyDisplay::drawPolyline (CDrawContext* context, const CRect& plot,
                               const std::function<double (double)>& sampler,
                               const CColor& colour, CCoord width)
{
	context->setFrameColor (colour);
	context->setLineWidth (width);

	SharedPointer<CGraphicsPath> path = owned (context->createGraphicsPath ());
	if (path == nullptr)
		return;

	const auto pointAt = [&] (double fraction, double decibels)
	{ return CPoint (xOf (fraction, plot), yOf (decibels, plot)); };

	// Where the segment between two samples crosses the floor. Linear in
	// decibels against the horizontal position, which is what the plot's
	// own axes are.
	const auto crossing = [&] (double xA, double dbA, double xB, double dbB)
	{
		const double span = dbA - dbB;
		const double f = (span > 1e-12) ? (dbA - kMinDb) / span : 0.0;
		const double t = std::min (1.0, std::max (0.0, f));
		return pointAt (xA + (xB - xA) * t, kMinDb);
	};

	bool drawing = false;
	double prevX = 0.0, prevDb = 0.0;

	for (int i = 0; i < kPoints; ++i)
	{
		const double x = i / static_cast<double> (kPoints - 1);
		const double decibels = sampler (x);

		if (decibels < kMinDb)
		{
			if (drawing)
			{
				path->addLine (crossing (prevX, prevDb, x, decibels));
				drawing = false;
			}
		}
		else if (! drawing)
		{
			if (i > 0)
			{
				path->beginSubpath (crossing (x, decibels, prevX, prevDb));
				path->addLine (pointAt (x, decibels));
			}
			else
			{
				path->beginSubpath (pointAt (x, decibels));
			}
			drawing = true;
		}
		else
		{
			path->addLine (pointAt (x, decibels));
		}

		prevX  = x;
		prevDb = decibels;
	}

	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

//------------------------------------------------------------------------
void SpyDisplay::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	context->setDrawMode (kAntiAliasing);

	// The plate: a dark ground with a thin light edge, so the display
	// reads as a window into the plug-in rather than as a hole in the
	// panel.
	context->setFillColor (kPlate);
	context->setFrameColor (kPlateEdge);
	context->setLineWidth (1.);
	CRect plate (r);
	plate.inset (0.5, 0.5);
	context->drawRect (plate, kDrawFilledAndStroked);

	// The caption gets a band of its own at the top; without it a trace at
	// full level runs through the lettering.
	CRect plot (r);
	plot.inset (6., 5.);
	plot.top += 12.;

	drawGrid (context, plot);

	CRect caption (r);
	caption.inset (5., 4.);
	caption.bottom = caption.top + 11.;

	context->setFont (kNormalFontVerySmall);
	context->setFontColor (kCaption);

	// THE TRANSPORT, in words, because it is the answer to "why has the
	// pad I clicked not started". "No transport" is not a fault: it is a
	// host that reports none, in which case pads launch at once and the
	// bar ruler below is decoration.
	{
		const char* state = "No transport";
		if (mTransport == kTransportPlaying)
			state = "Playing";
		else if (mTransport == kTransportStopped)
			state = "Stopped";

		char buffer[64];
		std::snprintf (buffer, sizeof (buffer), "%s  -  %d/bar", state, mBeatsPerBar);
		context->drawString (buffer, caption, kLeftText, true);
	}

	// The rate the DSP is actually running at, in the corner. It is here
	// because it is the one fact the panel has that the host's own generic
	// view does not, and because a wrong one is the first thing to suspect
	// when a sample plays at the wrong pitch.
	{
		char buffer[32];
		std::snprintf (buffer, sizeof (buffer), "%.4g kHz", mSampleRate / 1000.0);
		context->drawString (buffer, caption, kRightText, true);
	}

	// THE ONE THING THERE IS TO DRAW. dbToLinear and linearToDb are the
	// output stage's own pair, not a local conversion - so a trim at the
	// bottom of its travel reads as OFF here for exactly the same reason
	// it is silent there, and the two cannot come to different views about
	// where "off" is.
	drawPolyline (context, plot,
		[this] (double)
		{
			const double gain = dbToLinear (mTrimDb, kTrimMinDb);
			return linearToDb (gain, kMinDb - 1.0);   // below the floor = break
		},
		kOutputTrace, 2.0);

	// LAST, over everything, because it is the only thing that moves.
	drawPlayhead (context, plot);

	setDirty (false);
}

//------------------------------------------------------------------------
} // namespace Project6
