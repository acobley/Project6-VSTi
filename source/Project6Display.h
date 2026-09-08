//------------------------------------------------------------------------
// Project6 - the panel display
//
// ONE BAR WIDE AND THE TRIM'S RANGE TALL.
//
// It began as VocalFilter's SpyResponseDisplay with everything
// vocal-tract-specific taken out, and its horizontal axis carried no
// quantity at all - the grid was even thirds, waiting for something to
// mean. The transport gave it one: the plot is now ONE BAR, ruled into
// beats, with a playhead sweeping it. That is what tells a person why a
// pad they clicked has not started yet, which is the question a panel
// that launches on the bar has to be able to answer.
//
// The vertical axis is still decibels, with the output trim drawn across
// it. The plate, the caption band and drawPolyline() - which breaks a
// curve at the bottom of the scale instead of clamping it - are as they
// were.
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

	/** Everything the panel shows: the output trim in decibels, the rate
	    the DSP is running at, the transport state as a TransportDisplay,
	    where the transport is through the current bar, and how many beats
	    that bar has.

	    Returns true if anything moved, so the editor's timer can skip the
	    redraw when nothing has - which matters more now that a playhead
	    means the answer is usually yes while the transport rolls and
	    always no while it does not. */
	bool setState (double trimDb, double sampleRate, int transport,
	               double barPhase, int beatsPerBar);

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

	void drawPlayhead (VSTGUI::CDrawContext* context, const VSTGUI::CRect& plot);

	double mTrimDb      = kTrimDefaultDb;
	double mSampleRate  = 44100.0;
	int    mTransport   = 0;          ///< a TransportDisplay
	double mBarPhase    = 0.0;
	int    mBeatsPerBar = 4;
};

//------------------------------------------------------------------------
} // namespace Project6
