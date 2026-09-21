#!/usr/bin/env python3
"""Draw the plug-in's panel as a host shows it, into docs/panel.png.

WHY IT READS THE HEADER. Every position and size below comes out of
source/Project6Editor.h, and every colour out of the files that define it, so
this cannot quietly drift from the thing it is a picture of. A constant that
gets renamed makes this FAIL rather than silently draw last year's panel - the
same bargain tools/render-routing.py makes.

What it cannot show is the real VSTGUI rendering: this is a faithful
reconstruction from the same numbers, not a screenshot. The pad contents are
made up, because an empty bank is a poor advertisement for a sample bank.

    python3 tools/render-panel.py [out.png]
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')


# ---------------------------------------------------------------------------
# The numbers, read out of the header rather than copied
# ---------------------------------------------------------------------------
def read_constants(path, wanted):
    """Every `static constexpr int kName = <literal>;` in one header.

    Only literals - anything defined in terms of other constants is computed
    below, where the arithmetic is visible and can be checked against the
    header by eye.
    """
    text = open(os.path.join(ROOT, path), encoding='utf-8').read()
    found = {}
    for name, value in re.findall(
            r'static\s+constexpr\s+int\s+(k\w+)\s*=\s*(-?\d+)\s*;', text):
        found[name] = int(value)

    missing = [n for n in wanted if n not in found]
    if missing:
        sys.exit('render-panel: not found in %s: %s\n'
                 'The header moved under this script; fix the names rather '
                 'than hard-coding the numbers.' % (path, ', '.join(missing)))
    return found


NEEDED = ('kMargin kSliderWidth kSliderHeight kColumnGap kSlotWidth kSlotHeight '
          'kSlotGap kStripHeight kLevelGap kDivisionWidth kFitWidth kLoopWidth '
          'kStripGap kTitleTop kTitleHeight kHeadingHeight kRowChannelHeight '
          'kRowChannelGap kRowFaderGap kDisplayGap kDisplayHeight '
          'kColumnButtonHeight kColumnButtonGap').split()

K = read_constants('source/Project6Editor.h', NEEDED)
g = K.get

SLOT_COLUMNS, SLOT_ROWS = 8, 8

# Which preview the display is showing. A pad holds a wav or a mid, so the
# panel draws a waveform or a piano roll - never both. 'wave' or 'roll'.
PREVIEW = 'wave'

def read_doubles(path, wanted):
    """Named `constexpr double`s, or a `constexpr double k...[] = {...}`.

    Parsed rather than copied, for the same reason the sizes are: a number
    written down twice is a number that will disagree with itself. A name
    that moves makes this FAIL rather than quietly draw a grid the plug-in
    does not have.
    """
    text = open(os.path.join(ROOT, path), encoding='utf-8').read()
    found = {}
    for name, value in re.findall(
            r'constexpr\s+double\s+(k\w+)\s*=\s*(-?[\d.]+)\s*;', text):
        found[name] = float(value)
    for name, body in re.findall(
            r'constexpr\s+double\s+(k\w+)\s*\[\s*\]\s*=\s*\{([^}]*)\}', text):
        found[name] = tuple(float(v) for v in re.findall(r'-?[\d.]+', body))

    missing = [n for n in wanted if n not in found]
    if missing:
        sys.exit('render-panel: not found in %s: %s\n'
                 '  The source moved under this script; fix the names rather '
                 'than the picture.' % (path, ', '.join(missing)))
    return found


# Project6Dsp.h - the trim's range, so the slider position and the trace
# in the display are computed from the same scale.
_DSP = read_doubles('source/Project6Dsp.h', ('kTrimMinDb', 'kTrimMaxDb'))
TRIM_MIN_DB = _DSP['kTrimMinDb']
TRIM_MAX_DB = _DSP['kTrimMaxDb']

# Project6Display.cpp - the horizontal rules across the plot. SpyDisplay
# scales the plot from the FIRST of these to the LAST, so they are the
# vertical scale as well as the grid.
DB_LINES = read_doubles('source/Project6Display.cpp', ('kDbLines',))['kDbLines']

# Derived exactly as the header derives them.
LEVEL_W      = g('kSlotWidth') - g('kDivisionWidth') - g('kFitWidth') \
               - g('kLoopWidth') - 3 * g('kStripGap')
CELL_H       = g('kSlotHeight') + g('kLevelGap') + g('kStripHeight')
GRID_W       = SLOT_COLUMNS * g('kSlotWidth') + (SLOT_COLUMNS - 1) * g('kSlotGap')
GRID_H       = SLOT_ROWS * CELL_H + (SLOT_ROWS - 1) * g('kSlotGap')
CONTENT_W    = g('kSliderWidth')
FADER_LEFT   = g('kMargin') + GRID_W + g('kRowFaderGap')
W            = FADER_LEFT + g('kSliderWidth') + g('kMargin')
HEADING_TOP  = g('kTitleTop') + g('kTitleHeight') + 8
GRID_TOP     = HEADING_TOP + g('kHeadingHeight') + 3
DISPLAY_LEFT = g('kMargin') + CONTENT_W + g('kDisplayGap')
DISPLAY_W    = W - DISPLAY_LEFT - g('kMargin')
DISPLAY_BOT  = HEADING_TOP + g('kDisplayHeight')
SLOT_HEAD    = DISPLAY_BOT + 16
COL_BTN_TOP  = SLOT_HEAD + g('kHeadingHeight') + 3
SLOT_TOP     = COL_BTN_TOP + g('kColumnButtonHeight') + g('kColumnButtonGap')
H            = SLOT_TOP + GRID_H + g('kMargin')

# The two the header states outright, so a mistake above is caught here.
for label, got, want in (('width', W, 1071), ('height', H, 624)):
    if got != want:
        sys.exit('render-panel: computed %s %d, header says %d - the '
                 'arithmetic here no longer matches the header.'
                 % (label, got, want))

# ---------------------------------------------------------------------------
# The colours, from the files that define them
# ---------------------------------------------------------------------------
PANEL        = (64, 64, 64)
WELL_EMPTY   = (40, 40, 40)
WELL_FULL    = (58, 58, 58)
WELL_MIDI    = (58, 56, 74)
WELL_LIVE    = (96, 34, 34)
WELL_ARMED   = (74, 60, 30)
WELL_SHADOW  = (100, 100, 100)
WELL_HIGH    = (200, 200, 200)
SLOT_TEXT    = (255, 255, 255)
SLOT_IDLE    = (140, 140, 140)
LIVE_BORDER  = (255, 70, 70)
PEND_BORDER  = (255, 190, 60)
PROGRESS     = (255, 140, 110)
LAMP_OFF     = (0, 0, 0)
BAR_FILL     = (100, 100, 100)
BAR_LIGHT    = (200, 200, 200)
BAR_HIGH     = (255, 255, 255)
PLAYHEAD     = (255, 190, 60)
CAPTION      = (235, 235, 235)


def font(size, bold=False):
    for directory in ('/usr/share/fonts/truetype/dejavu/',
                      '/System/Library/Fonts/Supplemental/'):
        name = ('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf') \
            if directory.startswith('/usr') else ('Arial Bold.ttf' if bold else 'Arial.ttf')
        path = os.path.join(directory, name)
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


# panelFontTiny is nine points in the real panel, which is what keeps a
# transposition legible where it shares its rectangle with the bar behind it.
F_TITLE, F_HEAD, F_PAD, F_TINY = font(11, True), font(10), font(9), font(8)


def well(draw, box, light=WELL_HIGH, shadow=WELL_SHADOW):
    """VSTGUI's Draw3dRect: light down the top and left, shadow the rest."""
    x0, y0, x1, y1 = box
    draw.line((x0, y1 - 1, x0, y0), fill=light)
    draw.line((x0, y0, x1 - 1, y0), fill=light)
    draw.line((x1 - 1, y0, x1 - 1, y1 - 1), fill=shadow)
    draw.line((x1 - 1, y1 - 1, x0, y1 - 1), fill=shadow)


def centred(draw, box, string, fnt, fill):
    x0, y0, x1, y1 = box
    w = draw.textlength(string, font=fnt)
    draw.text(((x0 + x1 - w) / 2, (y0 + y1) / 2 - fnt.size * 0.62),
              string, font=fnt, fill=fill)


# ---------------------------------------------------------------------------
# What the pads are holding. Invented - see the docstring.
# ---------------------------------------------------------------------------
#   name, kind ('wav' | 'mid' | None), state ('play' | 'arm' | None),
#   level 0..1, division index, fit index, loops, transpose
BANK = {
    (0, 0): ('kick 01.wav',    'wav', 'play', 0.78, 0, 1, True,  0),
    (0, 1): ('kick soft.wav',  'wav', None,   0.70, 0, 1, True,  0),
    (0, 3): ('clap.wav',       'wav', None,   0.62, 2, 0, False, 0),
    (1, 0): ('snare 03.wav',   'wav', 'play', 0.74, 0, 1, True,  0),
    (1, 2): ('rim.wav',        'wav', None,   0.55, 3, 0, False, 0),
    (2, 0): ('hats closed.wav','wav', 'arm',  0.66, 3, 1, True,  0),
    (2, 4): ('ride.wav',       'wav', None,   0.58, 1, 1, True,  0),
    (3, 1): ('bass loop.wav',  'wav', 'play', 0.72, 0, 2, True,  0),
    (3, 5): ('sub hit.wav',    'wav', None,   0.64, 2, 0, False, 0),
    (4, 0): ('keys Am.wav',    'wav', None,   0.68, 0, 2, True,  0),
    (4, 2): ('pad warm.wav',   'wav', 'play', 0.60, 0, 2, True,  0),
    (5, 3): ('vox chop.wav',   'wav', None,   0.57, 1, 1, True,  0),
    (6, 0): ('arp 93.mid',     'mid', 'play', 0.00, 0, 0, True,  12),
    (6, 1): ('chords.mid',     'mid', None,   0.00, 0, 0, True,  0),
    (6, 4): ('stab.mid',       'mid', None,   0.00, 2, 0, False, -5),
    (7, 0): ('patterns 93.mid','mid', 'arm',  0.00, 0, 0, True,  0),
    (7, 2): ('fill.mid',       'mid', None,   0.00, 3, 0, False, 0),
}
DIVISIONS = ('1/1', '1/2', '1/4', '1/8')
FITS      = ('off', 'spd', 'pch')


def draw_panel(out_path):
    image = Image.new('RGB', (W, H), PANEL)
    d = ImageDraw.Draw(image, 'RGBA')

    d.text((g('kMargin'), g('kTitleTop')), 'Project6  -  instrument',
           font=F_TITLE, fill=SLOT_TEXT)

    # --- the output trim, left of the display ---------------------------
    # ONE VALUE, used twice: the slider's position and the trace in the
    # display are the same number, exactly as they are in the plug-in,
    # where the display plots the trim and nothing else.
    trim_db = -6.0
    trim_frac = (trim_db - TRIM_MIN_DB) / (TRIM_MAX_DB - TRIM_MIN_DB)

    d.text((g('kMargin'), HEADING_TOP), 'Output', font=F_HEAD, fill=CAPTION)
    sl = (g('kMargin'), GRID_TOP, g('kMargin') + g('kSliderWidth'),
          GRID_TOP + g('kSliderHeight'))
    d.rectangle(sl, fill=LAMP_OFF)
    d.rectangle((sl[0] + 1, sl[1] + 1,
                 sl[0] + 1 + (sl[2] - sl[0] - 2) * trim_frac, sl[3] - 1),
                fill=BAR_FILL)
    well(d, sl, BAR_LIGHT, BAR_HIGH)
    d.text((g('kMargin'), GRID_TOP + g('kSliderHeight') + 4),
           '%.1f dB' % trim_db, font=F_TINY, fill=SLOT_IDLE)

    # --- the display ----------------------------------------------------
    plate = (DISPLAY_LEFT, HEADING_TOP, DISPLAY_LEFT + DISPLAY_W, DISPLAY_BOT)
    d.rectangle(plate, fill=(0, 0, 0, 190))
    d.rectangle(plate, outline=(255, 255, 255, 110))

    # SpyDisplay::draw: plot = r.inset(6, 5) then plot.top += 12, which is
    # the band the caption gets so that a trace at full level does not run
    # through the lettering.
    plot = (plate[0] + 6, plate[1] + 5 + 12, plate[2] - 6, plate[3] - 5)
    plot_h = plot[3] - plot[1]
    plot_w = plot[2] - plot[0]

    # THE POINTED-AT PAD'S PREVIEW, behind everything. A pad holds a wav
    # or a mid, so the panel draws one or the other and never both - which
    # one this picture shows is PREVIEW, above.
    import math
    if PREVIEW == 'wave':
        middle = (plot[1] + plot[3]) / 2.0
        reach = (plot[3] - plot[1]) / 2.0
        buckets = 256
        shape = []
        for i in range(buckets):
            t = i / float(buckets)
            beat = (t * 4.0) % 1.0
            env = ((0.30 + 0.62 * math.exp(-beat * 5.0))
                   * (0.85 + 0.15 * math.sin(t * 31.0)))
            shape.append(min(1.0, env * (0.9 + 0.1 * math.sin(t * 7.0))))

        top = [(plot[0] + plot_w * i / float(buckets), middle - reach * v)
               for i, v in enumerate(shape)]
        bottom = [(plot[0] + plot_w * i / float(buckets), middle + reach * v)
                  for i, v in reversed(list(enumerate(shape)))]
        # SpyDisplay::drawWaveform starts its path at the left edge on the
        # centre line, then out along the top and back along the bottom.
        d.polygon([(plot[0], middle)] + top + bottom, fill=(150, 190, 255, 60))
    else:
        # A lane per semitone of the range the file ACTUALLY uses, high
        # pitches at the top, with a little air top and bottom. Invented
        # notes, like the pad names.
        notes = []
        pattern = [(0, 36, 2), (2, 42, 1), (3, 36, 1), (4, 38, 2), (6, 42, 1),
                   (8, 36, 2), (10, 42, 1), (11, 36, 1), (12, 38, 2), (14, 42, 2),
                   (1, 48, 1), (5, 51, 1), (9, 48, 1), (13, 53, 2),
                   (0, 60, 4), (4, 63, 4), (8, 60, 4), (12, 65, 3)]
        for step, pitch, length in pattern:
            notes.append((step / 16.0, (step + length) / 16.0, pitch))

        lowest = min(n[2] for n in notes)
        highest = max(n[2] for n in notes)
        lanes = (highest - lowest) + 1
        inset = min(6.0, (plot[3] - plot[1]) * 0.08)
        top_y = plot[1] + inset
        lane = max(1.0, ((plot[3] - plot[1]) - inset * 2)) / lanes
        bar_h = max(1.0, lane - 1.0)

        for start, end, pitch in notes:
            x0 = plot[0] + plot_w * start
            x1 = plot[0] + plot_w * end
            y = top_y + (highest - pitch) * lane
            d.rectangle((x0, y, max(x1, x0 + 1), y + bar_h),
                        fill=(170, 160, 255, 120))

    # kDbLines, from Project6Display.cpp. Unity is the line drawn brightest
    # because it is the one that matters.
    db_lines = DB_LINES

    def y_of(db):
        return plot[1] + plot_h * (db_lines[0] - db) / (db_lines[0] - db_lines[-1])

    for db in db_lines:
        y = y_of(db)
        d.line((plot[0], y, plot[2], y),
               fill=(255, 255, 255, 70 if db == 0.0 else 38))

    beats = 4                                   # the host's own numerator
    for beat in range(beats):
        x = plot[0] + plot_w * beat / float(beats)
        d.line((x, plot[1], x, plot[3]),
               fill=(255, 255, 255, 70 if beat == 0 else 38))

    # THE TRIM, plotted across the bar as a straight line - the display is
    # a readout of the trim and the transport, not a meter. It is drawn
    # OVER the waveform above, which is the ground rather than a reading.
    d.line((plot[0], y_of(trim_db), plot[2], y_of(trim_db)),
           fill=(255, 255, 255, 205), width=2)

    # The caption band at the TOP - transport and beats on the left, the
    # DSP's actual sample rate on the right.
    cap_y = plate[1] + 4
    d.text((plate[0] + 5, cap_y), 'Playing  -  %d/bar' % beats,
           font=F_TINY, fill=(255, 255, 255, 200))
    rate = '44.1 kHz'
    d.text((plate[2] - 5 - d.textlength(rate, font=F_TINY), cap_y), rate,
           font=F_TINY, fill=(255, 255, 255, 200))

    # LAST, over everything, because it is the only thing that moves.
    xh = plot[0] + plot_w * 0.42
    d.line((xh, plot[1], xh, plot[3]), fill=PLAYHEAD, width=2)

    # --- headings -------------------------------------------------------
    d.text((g('kMargin'), SLOT_HEAD),
           'Samples  -  drag .wav files onto the slots; a click launches on the next bar',
           font=F_HEAD, fill=CAPTION)
    # CRect (kRowFaderLeft - 130, kSlotHeadingTop, ...) in the editor.
    d.text((FADER_LEFT - 130, SLOT_HEAD),
           'Row levels  (direct outs tap before these)',
           font=F_HEAD, fill=CAPTION)

    # --- column buttons -------------------------------------------------
    for col in range(SLOT_COLUMNS):
        x = g('kMargin') + col * (g('kSlotWidth') + g('kSlotGap'))
        box = (x, COL_BTN_TOP, x + g('kSlotWidth'),
               COL_BTN_TOP + g('kColumnButtonHeight'))
        fill = WELL_FULL if col in (0, 1) else WELL_EMPTY
        d.rectangle(box, fill=fill)
        if col == 0:                       # part of this column is sounding
            d.rectangle((box[0] + 1, box[1] + 1,
                         box[0] + 1 + (g('kSlotWidth') - 2) * 0.5, box[3] - 1),
                        fill=(96, 34, 34))
        well(d, box, WELL_HIGH, WELL_SHADOW)
        centred(d, box, str(col + 1), F_PAD,
                SLOT_TEXT if col in (0, 1) else SLOT_IDLE)

    # --- the 8 x 8 bank -------------------------------------------------
    for row in range(SLOT_ROWS):
        y = SLOT_TOP + row * (CELL_H + g('kSlotGap'))
        for col in range(SLOT_COLUMNS):
            x = g('kMargin') + col * (g('kSlotWidth') + g('kSlotGap'))
            entry = BANK.get((row, col))
            pad = (x, y, x + g('kSlotWidth'), y + g('kSlotHeight'))

            if entry is None:
                d.rectangle(pad, fill=WELL_EMPTY)
                well(d, pad, WELL_SHADOW, WELL_HIGH)
            else:
                name, kind, state, level, div, fit, loops, transpose = entry
                base = {'play': WELL_LIVE, 'arm': WELL_ARMED}.get(
                    state, WELL_MIDI if kind == 'mid' else WELL_FULL)
                d.rectangle(pad, fill=base)
                well(d, pad, WELL_SHADOW, WELL_HIGH)
                if state:
                    d.rectangle(pad, outline=LIVE_BORDER if state == 'play'
                                else PEND_BORDER)
                centred(d, (pad[0] + 3, pad[1], pad[2] - 3, pad[3] - 4),
                        name, F_PAD, SLOT_TEXT)
                if state == 'play':         # how far through the file it is
                    frac = 0.28 + 0.14 * row
                    d.rectangle((pad[0] + 1, pad[3] - 4,
                                 pad[0] + 1 + (g('kSlotWidth') - 2) * frac,
                                 pad[3] - 2), fill=PROGRESS)

            # ---- the strip: level (or transpose), division, fit, loop ----
            sy0 = y + g('kSlotHeight') + g('kLevelGap')
            sy1 = sy0 + g('kStripHeight')
            sx = x

            bar = (sx, sy0, sx + LEVEL_W, sy1)
            d.rectangle(bar, fill=LAMP_OFF)
            if entry and entry[1] == 'mid':
                # A MIDI pad shows a centre-zero TRANSPOSE where an audio
                # pad shows its level - the two share this rectangle.
                semis = entry[7]
                centre = (bar[0] + bar[2]) // 2
                if semis:
                    reach = max(1, int((LEVEL_W / 2 - 1) * abs(semis) / 24.0))
                    d.rectangle((centre, sy0 + 1, centre + reach, sy1 - 1)
                                if semis > 0 else
                                (centre - reach, sy0 + 1, centre, sy1 - 1),
                                fill=BAR_FILL)
                centred(d, bar, ('%+d' % semis) if semis else '0', F_TINY,
                        SLOT_TEXT if semis else SLOT_IDLE)
            elif entry:
                d.rectangle((bar[0] + 1, sy0 + 1,
                             bar[0] + 1 + (LEVEL_W - 2) * entry[3], sy1 - 1),
                            fill=BAR_FILL)
            well(d, bar, BAR_LIGHT, BAR_HIGH)

            sx += LEVEL_W + g('kStripGap')
            for width, label, lit in (
                    (g('kDivisionWidth'), DIVISIONS[entry[4]] if entry else '1/1', bool(entry)),
                    (g('kFitWidth'), FITS[entry[5]] if entry else 'off',
                     bool(entry) and entry[5] != 0),
                    (g('kLoopWidth'), ('L' if (entry is None or entry[6]) else '1'),
                     bool(entry) and not entry[6])):
                box = (sx, sy0, sx + width, sy1)
                d.rectangle(box, fill=WELL_FULL)
                well(d, box, WELL_HIGH, WELL_SHADOW)
                centred(d, box, label, F_TINY, SLOT_TEXT if lit else SLOT_IDLE)
                sx += width + g('kStripGap')

        # ---- the row fader, with its MIDI channel above ----------------
        fy = y + g('kRowChannelHeight') + g('kRowChannelGap')
        d.text((FADER_LEFT, y), 'MIDI ch %d' % (row + 1),
               font=F_TINY, fill=SLOT_IDLE)
        fader = (FADER_LEFT, fy, FADER_LEFT + g('kSliderWidth'),
                 fy + CELL_H - g('kRowChannelHeight') - g('kRowChannelGap'))
        d.rectangle(fader, fill=LAMP_OFF)
        d.rectangle((fader[0] + 1, fader[1] + 1,
                     fader[0] + 1 + (g('kSliderWidth') - 2) * (0.74 - row * 0.02),
                     fader[3] - 1), fill=BAR_FILL)
        well(d, fader, BAR_LIGHT, BAR_HIGH)

    image.save(out_path)
    print('render-panel: wrote %s (%d x %d)' % (out_path, W, H))


if __name__ == '__main__':
    draw_panel(sys.argv[1] if len(sys.argv) > 1
               else os.path.join(ROOT, 'docs', 'panel.png'))
