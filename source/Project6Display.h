//------------------------------------------------------------------------
// Project6 - the panel display
//
// AN EMPTY CANVAS, on purpose. It is VocalFilter's SpyResponseDisplay with
// everything vocal-tract-specific taken out: the plate, the border, the
// caption band, the decibel grid and - the part that was expensive to get
// right - drawPolyline(), which breaks a curve at the bottom of the scale
// instead of clamping it. What it has to draw is one horizontal line at
// the output trim, because that is the only thing this plug-in currently
// computes.
//
// It is NOT in Project6Controls.*, deliberately. That file is a control
// set carried across three plug-ins and it knows nothing about any of
// them; this view is where the knowledge about THIS plug-in goes.
//
// WHEN YOU PUT A REAL CURVE HERE, take it from a function in
// Project6Dsp.h that the DSP also calls. Anything the editor displays that
// the DSP computes must come from ONE shared function, or the panel agrees
// with the audio today and diverges at some sample rate nobody tests. The
// line below already obeys that rule for the one value there is: the trim
// is converted by dbToLinear() and back by linearToDb(), the same pair the
// output stage uses.
//------------------------------------------------------------------------

#pragma once

#include "Project6Dsp.h"

#include "vstgui/vstgui.h"

#include <functional>

namespace Project6 {

//------------------------------------------------------------------------
class SpyDisplay : public VSTGUI::CView
{
public:
	explicit SpyDisplay (const VSTGUI::CRect& size);

	/** What the panel is showing: the output trim in decibels, and the
	    rate the DSP is running at.

	    The sample rate is carried even though nothing drawn yet depends on
	    it, because the first thing that does will be a filter shape - a
	    function of f/fs - and a display that had to start assuming 44.1 k
	    would be drawing a filter nobody is hearing.

	    Returns true if anything moved, so the editor's timer can skip the
	    redraw when nothing has. */
	bool setLevel (double trimDb, double sampleRate);

	void draw (VSTGUI::CDrawContext* context) override;

	CLASS_METHODS (SpyDisplay, VSTGUI::CView)

	/** The vertical scale. +12 dB of headroom above unity so a boost added
	    later has somewhere to go, and a floor 12 dB below the bottom of
	    the trim's own travel so a control at its minimum is visibly OFF
	    rather than sitting on the frame. */
	static constexpr double kMaxDb = kTrimMaxDb + 12.0;
	static constexpr double kMinDb = kTrimMinDb - 12.0;

private:
	VSTGUI::CCoord xOf (double fraction, const VSTGUI::CRect& plot) const;
	VSTGUI::CCoord yOf (double decibels, const VSTGUI::CRect& plot) const;

	void drawGrid (VSTGUI::CDrawContext* context, const VSTGUI::CRect& plot);

	/** One broken polyline across the plot. `sampler` returns decibels for
	    a position 0..1 across the width; anything below the floor BREAKS
	    the line rather than being clamped to it.
	 *
	 *  Clamping draws a flat line along the bottom, which reads as
	 *  something quiet happening across the whole plot rather than as
	 *  nothing happening at all. And the break is taken TO THE EDGE by
	 *  interpolating the crossing, not to the last sample that happened to
	 *  be on the scale: dropping the sample instead ends the line in clear
	 *  air part-way up, and a curve that stops in mid-plot looks like a
	 *  curve that turned round.
	 *
	 *  Everything this view draws goes through here, so two curves cannot
	 *  end up drawn by two slightly different pieces of code. */
	void drawPolyline (VSTGUI::CDrawContext* context, const VSTGUI::CRect& plot,
	                   const std::function<double (double)>& sampler,
	                   const VSTGUI::CColor& colour, VSTGUI::CCoord width);

	double mTrimDb     = kTrimDefaultDb;
	double mSampleRate = 44100.0;
};

//------------------------------------------------------------------------
} // namespace Project6
