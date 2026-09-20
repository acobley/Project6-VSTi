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

/** The armed pad's waveform, BEHIND the grid. Dim on purpose: it is
    context, not a readout, and the trim trace and the playhead are the
    two things on this display that have to be read exactly. A waveform
    bright enough to compete with them would make the display harder to
    use in exchange for looking busier. */
const CColor kWaveform (150, 190, 255, 60);

/** A MIDI pad's notes. Violet, because that is the colour a MIDI pad's
    well already is on the grid - so the display and the pad you are
    pointing at agree about what kind of file this is without a word being
    written. Brighter than the waveform because a handful of thin bars
    needs more to read as a shape than a solid silhouette does, and still
    dim enough to stay behind the trim trace and the playhead. */
const CColor kRollNote (170, 160, 255, 120);

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
bool SpyDisplay::setWaveform (const std::vector<float>& peaks)
{
	if (peaks == mWaveform)
		return false;

	mWaveform = peaks;
	invalid ();
	return true;
}

//------------------------------------------------------------------------
void SpyDisplay::drawWaveform (CDrawContext* context, const CRect& plot)
{
	if (mWaveform.empty ())
		return;

	SharedPointer<CGraphicsPath> path = owned (context->createGraphicsPath ());
	if (path == nullptr)
		return;

	// MIRRORED ABOUT THE MIDDLE, which is what a waveform looks like and
	// is not what this plot's dB scale means. That is deliberate: the
	// silhouette is a picture of the file, drawn in the space the plot
	// happens to occupy, and dressing it up in decibels would invite it
	// to be read against the gridlines it has nothing to do with.
	const CCoord middle = (plot.top + plot.bottom) * 0.5;
	const CCoord reach  = plot.getHeight () * 0.5;
	const auto   count  = static_cast<double> (mWaveform.size ());

	const auto xAt = [&] (std::size_t i)
	{ return plot.left + plot.getWidth () * (static_cast<double> (i) / count); };

	path->beginSubpath (CPoint (plot.left, middle));
	for (std::size_t i = 0; i < mWaveform.size (); ++i)
		path->addLine (CPoint (xAt (i),
		                       middle - reach * std::min (1.f, mWaveform[i])));

	// Back along the bottom half in reverse, so the two halves close into
	// one filled shape rather than two strokes with a gap between them.
	for (std::size_t i = mWaveform.size (); i > 0; --i)
		path->addLine (CPoint (xAt (i - 1),
		                       middle + reach * std::min (1.f, mWaveform[i - 1])));

	path->closeSubpath ();

	context->setFillColor (kWaveform);
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);
}

//------------------------------------------------------------------------
bool SpyDisplay::setRoll (const std::vector<RollNote>& notes)
{
	// Compared field by field: RollNote is three floats and has no
	// operator==, and writing one for a struct used in exactly one
	// comparison would be more code than the comparison.
	const bool same =
		notes.size () == mRoll.size ()
		&& std::equal (notes.begin (), notes.end (), mRoll.begin (),
		               [] (const RollNote& a, const RollNote& b)
		               {
			               return a.start == b.start && a.end == b.end
			                      && a.pitch == b.pitch;
		               });

	if (same)
		return false;

	mRoll = notes;

	// THE PITCH RANGE, ONCE. "Notes in relation to each other" means the
	// rectangle is filled by whatever range the file actually uses: a
	// two-octave bass line and a two-octave melody both fill it, and a
	// file that sits on one note gets a line through the middle rather
	// than a bar at the bottom of a 128-note scale it never visits.
	mRollLowest  = 0.f;
	mRollHighest = 0.f;

	if (!mRoll.empty ())
	{
		mRollLowest = mRollHighest = mRoll.front ().pitch;
		for (const RollNote& note : mRoll)
		{
			mRollLowest  = std::min (mRollLowest, note.pitch);
			mRollHighest = std::max (mRollHighest, note.pitch);
		}
	}

	invalid ();
	return true;
}

//------------------------------------------------------------------------
void SpyDisplay::drawRoll (CDrawContext* context, const CRect& plot)
{
	if (mRoll.empty ())
		return;

	// ONE NOTE, OR ALL ON ONE PITCH: a range of zero would divide by it.
	// Widened to a whole octave so a single-pitch file draws its line
	// across the middle rather than filling the rectangle edge to edge.
	float lowest  = mRollLowest;
	float highest = mRollHighest;
	if (highest - lowest < 1.f)
	{
		const float middle = (lowest + highest) * 0.5f;
		lowest  = middle - 6.f;
		highest = middle + 6.f;
	}

	// A LANE PER SEMITONE of the range actually used, with a little air
	// top and bottom so the outermost notes are bars rather than edges.
	const double lanes  = static_cast<double> (highest - lowest) + 1.0;
	const double inset  = std::min (6., plot.getHeight () * 0.08);
	const double top    = plot.top + inset;
	const double height = std::max (1., plot.getHeight () - inset * 2.);
	const double lane   = height / lanes;

	// Thin lanes would vanish; one pixel is the floor.
	const double barHeight = std::max (1., lane - 1.);

	context->setFillColor (kRollNote);

	for (const RollNote& note : mRoll)
	{
		const double x0 = plot.left + plot.getWidth () * note.start;
		const double x1 = plot.left + plot.getWidth () * note.end;

		// HIGH PITCHES AT THE TOP, which is the one thing everybody
		// expects of a piano roll and the one thing a y axis measured
		// downwards gets wrong by default.
		const double laneIndex = static_cast<double> (highest - note.pitch);
		const double y = top + laneIndex * lane;

		// A SHORT NOTE IS STILL A NOTE. A sixteenth in a long pattern can
		// come to less than a pixel, and a bar of no width is a bar
		// nobody sees - which would quietly empty the busiest patterns.
		CRect bar (x0, y, std::max (x1, x0 + 1.0), y + barHeight);
		context->drawRect (bar, kDrawFilled);
	}
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

	// BEHIND EVERYTHING. The preview is the ground the readouts are drawn
	// on, so it goes down before the grid rather than over it. A pad holds
	// one kind of file, so at most one of these two draws anything.
	drawWaveform (context, plot);
	drawRoll (context, plot);

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
