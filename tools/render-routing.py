#!/usr/bin/env python3
"""Draw the plug-in's signal routing to a PNG, so it can be LOOKED AT.

    python3 tools/render-routing.py [out.png]        # default docs/routing.png

THE NUMBERS ARE NOT DUPLICATED HERE. Every range, default and count on the
diagram is PARSED out of source/Project6Dsp.h and source/Project6Slots.h and
evaluated in the order it is declared, so a diagram that disagrees with the
code is a diagram that has not been regenerated. If a constant becomes an
expression this cannot evaluate, the script fails loudly rather than quietly
drawing a routing that is not the one that ships.

The SHAPE of the path is written down here, though - it has to be. Change
where a gain sits in Project6Dsp::renderChunk and you must change the
corresponding line below and re-run this, exactly as you would update a
comment.
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

INK      = (27, 27, 27)
MUTED    = (110, 110, 110)
RULE     = (170, 170, 170)
BOX_FILL = (244, 244, 246)
PAD_FILL = (228, 233, 240)
GAIN_FILL= (250, 236, 214)
GAIN_EDGE= (196, 142, 46)
BUS_FILL = (222, 236, 226)
BUS_EDGE = (70, 130, 90)
SIGNAL   = (46, 96, 156)
WHITE    = (255, 255, 255)


# ---------------------------------------------------------------------------
# The constants, read out of the headers
# ---------------------------------------------------------------------------
def read_constants(*relative_paths):
    """constexpr NAME = EXPR; from each header, evaluated in declaration order."""
    values = {}
    pattern = re.compile(
        r'^constexpr\s+(?:double|int)\s+(\w+)\s*=\s*([^;]+);', re.M)

    for relative in relative_paths:
        path = os.path.join(ROOT, relative)
        with open(path, encoding='utf-8') as handle:
            text = handle.read()

        for name, expr in pattern.findall(text):
            cleaned = expr.strip()
            try:
                values[name] = eval(cleaned, {'__builtins__': {}}, dict(values))
            except Exception as error:                       # noqa: BLE001
                raise SystemExit(
                    'render-routing: cannot evaluate %s = %s in %s (%s).\n'
                    '  The diagram must not guess. Teach this script the new '
                    'expression, or simplify the constant.'
                    % (name, cleaned, relative, error))

    return values


def require(values, *names):
    missing = [n for n in names if n not in values]
    if missing:
        raise SystemExit(
            'render-routing: these constants were not found in the headers: %s.\n'
            '  They have been renamed or removed, and the diagram would be '
            'drawing something that no longer exists.' % ', '.join(missing))
    return [values[n] for n in names]


def font(size, bold=False):
    names = ('DejaVuSans-Bold.ttf', 'DejaVuSans.ttf') if bold else ('DejaVuSans.ttf',)
    for name in names:
        for directory in ('/usr/share/fonts/truetype/dejavu/',
                          '/System/Library/Fonts/Supplemental/'):
            path = os.path.join(directory, name if directory.startswith('/usr')
                                else ('Arial Bold.ttf' if bold else 'Arial.ttf'))
            if os.path.exists(path):
                return ImageFont.truetype(path, size)
    return ImageFont.load_default()


# ---------------------------------------------------------------------------
# Drawing helpers
# ---------------------------------------------------------------------------
def text(draw, xy, string, fnt, fill=INK, anchor='la'):
    draw.text(xy, string, font=fnt, fill=fill, anchor=anchor)


def box(draw, rect, fill, edge, label=None, fnt=None, sub=None, subfnt=None):
    draw.rounded_rectangle(rect, radius=4, fill=fill, outline=edge, width=2)
    left, top, right, bottom = rect
    if label is not None:
        y = (top + bottom) / 2 - (7 if sub else 0)
        text(draw, ((left + right) / 2, y), label, fnt, anchor='mm')
    if sub is not None:
        text(draw, ((left + right) / 2, (top + bottom) / 2 + 9), sub, subfnt,
             fill=MUTED, anchor='mm')


def arrow(draw, x0, y0, x1, y1, colour=SIGNAL, width=2, head=7):
    draw.line((x0, y0, x1, y1), fill=colour, width=width)
    if x1 >= x0:
        draw.polygon([(x1, y1), (x1 - head, y1 - head * 0.55),
                      (x1 - head, y1 + head * 0.55)], fill=colour)
    else:
        draw.polygon([(x1, y1), (x1 + head, y1 - head * 0.55),
                      (x1 + head, y1 + head * 0.55)], fill=colour)


def elbow(draw, x0, y0, x1, y1, colour=SIGNAL, width=2, arrowhead=True):
    """Out, along, and in - so nothing is drawn diagonally across a label."""
    mid = x0 + (x1 - x0) * 0.45
    draw.line((x0, y0, mid, y0), fill=colour, width=width)
    draw.line((mid, y0, mid, y1), fill=colour, width=width)
    if arrowhead:
        arrow(draw, mid, y1, x1, y1, colour, width)
    else:
        draw.line((mid, y1, x1, y1), fill=colour, width=width)


def fader_glyph(draw, rect, fraction=0.72):
    """A level bar in the panel's own idiom: a well with a fill."""
    left, top, right, bottom = rect
    draw.rectangle(rect, fill=(60, 60, 62), outline=(150, 150, 150), width=1)
    draw.rectangle((left + 2, top + 2, left + 2 + (right - left - 4) * fraction,
                    bottom - 2), fill=(150, 165, 185))


# ---------------------------------------------------------------------------
def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'routing.png')

    values = read_constants('source/Project6Slots.h', 'source/Project6Dsp.h')
    (columns, rows, count,
     slot_min, slot_max, slot_default,
     row_min, row_max, row_default,
     trim_min, trim_max, trim_default,
     declick, smoothing) = require(
        values,
        'kSlotColumns', 'kSlotRows', 'kSlotCount',
        'kSlotLevelMinDb', 'kSlotLevelMaxDb', 'kSlotLevelDefaultDb',
        'kRowLevelMinDb', 'kRowLevelMaxDb', 'kRowLevelDefaultDb',
        'kTrimMinDb', 'kTrimMaxDb', 'kTrimDefaultDb',
        'kVoiceDeclickSeconds', 'kTrimSmoothingSeconds')

    W, H = 1520, 1062
    image = Image.new('RGB', (W, H), WHITE)
    draw = ImageDraw.Draw(image)

    f_title = font(30, bold=True)
    f_head  = font(17, bold=True)
    f_body  = font(14)
    f_small = font(12)
    f_tiny  = font(11)

    # --- heading ----------------------------------------------------------
    text(draw, (48, 34), 'Project6 — signal routing', f_title)
    text(draw, (48, 74),
         'Generated by tools/render-routing.py. Every number below is read out of '
         'source/Project6Dsp.h and source/Project6Slots.h — re-run it when the '
         'routing changes.', f_small, fill=MUTED)
    text(draw, (48, 92),
         'Outputs: the main stereo mix, plus %d aux stereo buses — one per row, '
         'tapped before that row’s fader.' % rows, f_small, fill=MUTED)
    draw.line((48, 112, W - 48, 112), fill=RULE, width=1)

    # --- geometry ---------------------------------------------------------
    pad_x0, pad_x1 = 62, 214
    lvl_x0, lvl_x1 = 252, 396
    bus_x = 448
    rowlvl_x0, rowlvl_x1 = 508, 668
    collect_x = 726
    mix_x = 812
    trim_x0, trim_x1 = 884, 1064
    out_x0, out_x1 = 1136, 1300

    top = 164
    pad_h, pad_gap = 34, 8
    expanded_h = rows * pad_h + (rows - 1) * pad_gap

    text(draw, (62, 130), 'ROW A, IN FULL — every row is the same', f_head)

    # --- the eight pads of row A, each through its own level --------------
    for i in range(columns):
        y0 = top + i * (pad_h + pad_gap)
        y1 = y0 + pad_h
        mid = (y0 + y1) / 2

        box(draw, (pad_x0, y0, pad_x1, y1), PAD_FILL, (120, 140, 170),
            'Pad A%d' % (i + 1), f_body)
        arrow(draw, pad_x1, mid, lvl_x0 - 2, mid)

        box(draw, (lvl_x0, y0, lvl_x1, y1), GAIN_FILL, GAIN_EDGE)
        fader_glyph(draw, (lvl_x0 + 14, mid - 6, lvl_x1 - 14, mid + 6),
                    0.30 + 0.085 * i)

        # into the row bus
        draw.line((lvl_x1, mid, bus_x, mid), fill=SIGNAL, width=2)

    # --- the row bus ------------------------------------------------------
    bus_top, bus_bottom = top + pad_h / 2, top + expanded_h - pad_h / 2
    bus_mid = (bus_top + bus_bottom) / 2
    draw.line((bus_x, bus_top, bus_x, bus_bottom), fill=SIGNAL, width=3)

    draw.ellipse((bus_x - 19, bus_mid - 19, bus_x + 19, bus_mid + 19),
                 fill=BUS_FILL, outline=BUS_EDGE, width=2)
    text(draw, (bus_x, bus_mid), 'Σ', font(22, bold=True), anchor='mm')
    text(draw, (bus_x, bus_bottom + 24), 'row A bus', f_small, fill=MUTED, anchor='mm')
    text(draw, ((lvl_x0 + lvl_x1) / 2, bus_bottom + 24), 'one level per pad', f_small,
         fill=MUTED, anchor='mm')

    arrow(draw, bus_x + 20, bus_mid, rowlvl_x0 - 2, bus_mid)

    # --- THE DIRECT OUT, which branches BEFORE the row level --------------
    # Drawn from a junction on the wire between the row bus and the row
    # fader, because that is exactly where it is taken.
    junction_x = bus_x + 40
    direct_y = top + 46
    direct_x0, direct_x1 = 700, 952

    draw.line((junction_x, bus_mid, junction_x, direct_y), fill=SIGNAL, width=2)
    arrow(draw, junction_x, direct_y, direct_x0 - 2, direct_y)
    draw.ellipse((junction_x - 4, bus_mid - 4, junction_x + 4, bus_mid + 4),
                 fill=SIGNAL, outline=SIGNAL)

    box(draw, (direct_x0, direct_y - 24, direct_x1, direct_y + 24),
        (232, 238, 232), (90, 130, 100), 'Row A direct out', f_head)
    text(draw, ((direct_x0 + direct_x1) / 2, direct_y + 40),
         'and one for each of the other %d rows — %d aux buses in all'
         % (rows - 1, rows), f_small, fill=MUTED, anchor='mm')

    # --- the row level ----------------------------------------------------
    box(draw, (rowlvl_x0, bus_mid - 30, rowlvl_x1, bus_mid + 30),
        GAIN_FILL, GAIN_EDGE, 'Row A level', f_head)
    fader_glyph(draw, (rowlvl_x0 + 20, bus_mid + 10, rowlvl_x1 - 20, bus_mid + 22), 0.62)

    # --- rows B..H, compressed -------------------------------------------
    strip_top = top + expanded_h + 62
    strip_h, strip_gap = 28, 8
    text(draw, (62, strip_top - 30), 'THE OTHER SEVEN ROWS, each identical', f_head)

    strip_mids = []
    for r in range(1, rows):
        y0 = strip_top + (r - 1) * (strip_h + strip_gap)
        y1 = y0 + strip_h
        mid = (y0 + y1) / 2
        strip_mids.append(mid)

        letter = chr(ord('A') + r)
        box(draw, (pad_x0, y0, rowlvl_x1, y1), BOX_FILL, (150, 150, 155))
        text(draw, (pad_x0 + 14, mid),
             'Row %s   —   %d pads  →  %d slot levels  →  Σ  →  direct out, '
             'then Row %s level'
             % (letter, columns, columns, letter), f_body, anchor='lm')

    # --- everything converges on ONE collector, then the mix -------------
    all_mids = [bus_mid] + strip_mids
    mix_mid = (min(all_mids) + max(all_mids)) / 2

    for mid in all_mids:
        draw.line((rowlvl_x1, mid, collect_x, mid), fill=SIGNAL, width=2)
    draw.line((collect_x, min(all_mids), collect_x, max(all_mids)),
              fill=SIGNAL, width=3)
    text(draw, (collect_x, max(all_mids) + 24), 'all %d rows' % rows, f_small,
         fill=MUTED, anchor='mm')

    arrow(draw, collect_x, mix_mid, mix_x - 21, mix_mid)

    draw.ellipse((mix_x - 20, mix_mid - 20, mix_x + 20, mix_mid + 20),
                 fill=BUS_FILL, outline=BUS_EDGE, width=2)
    text(draw, (mix_x, mix_mid), 'Σ', font(23, bold=True), anchor='mm')
    text(draw, (mix_x, mix_mid + 36), 'mix', f_small, fill=MUTED, anchor='mm')

    arrow(draw, mix_x + 21, mix_mid, trim_x0 - 2, mix_mid)

    # --- output trim and out ---------------------------------------------
    box(draw, (trim_x0, mix_mid - 34, trim_x1, mix_mid + 34),
        GAIN_FILL, GAIN_EDGE, 'Output trim', f_head)
    fader_glyph(draw, (trim_x0 + 24, mix_mid + 12, trim_x1 - 24, mix_mid + 24), 0.85)

    arrow(draw, trim_x1, mix_mid, out_x0 - 2, mix_mid)
    box(draw, (out_x0, mix_mid - 26, out_x1, mix_mid + 26),
        (232, 238, 232), (90, 130, 100), 'Stereo out', f_head)

    # --- the notes --------------------------------------------------------
    notes_y = strip_top + (rows - 1) * (strip_h + strip_gap) + 60
    draw.line((48, notes_y - 24, W - 48, notes_y - 24), fill=RULE, width=1)

    lines = [
        ('Slot level',   '%.0f to %+.0f dB, default %+.0f dB. Snapped when a voice '
                         'starts, smoothed thereafter.'
                         % (slot_min, slot_max, slot_default)),
        ('Row level',    '%.0f to %+.0f dB, default %+.0f dB, on the SUM of that '
                         'row’s %d pads. Snapped while the row is silent.'
                         % (row_min, row_max, row_default, columns)),
        ('Output trim',  '%.0f to %+.0f dB, default %+.0f dB — the one stage that '
                         'cannot boost, and where clipping is answered.'
                         % (trim_min, trim_max, trim_default)),
        ('Off means off', 'Every level reaches SILENCE at its minimum, not a small '
                          'number: a fader pulled all the way down is off.'),
        ('Smoothing',    'Levels %.0f ms one-pole; a voice fades in and out over '
                         '%.0f ms so a launch and a stop do not click.'
                         % (smoothing * 1000.0, declick * 1000.0)),
        ('Direct outs',  '%d aux stereo buses, one per row, tapped BEFORE that '
                         'row’s fader — so the fader balances the mix without '
                         'touching what leaves on the direct out.' % rows),
        ('Grid',         '%d columns × %d rows = %d slots. A slot’s ROW decides '
                         'which bus it sums into, and which direct out it leaves on.'
                         % (columns, rows, count)),
    ]

    for i, (name, body) in enumerate(lines):
        y = notes_y + i * 25
        text(draw, (48, y), name, f_head)
        text(draw, (232, y + 2), body, f_body)

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    image.save(out)
    print('render-routing: wrote %s (%d x %d)' % (out, W, H))
    return 0


if __name__ == '__main__':
    sys.exit(main())
