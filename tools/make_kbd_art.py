#!/usr/bin/env python3
"""Generates src/kbd_art.h: the printed legends of the EPS-16 Plus KEYBOARD model (the "keyboard"
skin of the SDL front end), as 4-bit alpha bitmaps.

Usage: python3 tools/make_kbd_art.py [path/to/FreeSans.ttf]

Positions are in "photo units": pixels of a top view photo of an EPS-16 Plus keyboard with an
OEX-6 output expander (2576 x 1445, the instrument spans x 60..2404, y 343..1102). The front end
maps them to the window (with a small vertical stretch that undoes the camera angle, see
kbd_ui.h). The legends are set in small capitals like the print on the unit (first letter of a
word full size, the rest smaller). Logos are left out.
"""
import sys, os, re
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/truetype/freefont/FreeSans.ttf'
BOLD = FONT.replace('FreeSans.ttf', 'FreeSansBold.ttf')
OUT = os.path.join(os.path.dirname(__file__), '..', 'src', 'kbd_art.h')
RES = 2.6             # texels per photo unit (the window shows ~0.6 px per unit)

GREY, WHITE, DIM = 0, 1, 2
L = []   # (text, x, y, size, colour, anchor, spacing, smallcaps, bold)
def lab(t, x, y, size=8.0, col=GREY, anchor='c', sp=0.0, sc=True, bold=False):
    L.append((t, x, y, size, col, anchor, sp, sc, bold))

# ---- rear edge (jack names, seen from above)
lab('Power', 663, 381, 6.5, DIM); lab('Line', 738, 381, 6.5, DIM); lab('Fuse', 820, 381, 6.5, DIM)
lab('MIDI', 1597.5, 375.5, 6.5, DIM)
lab('Thru', 1517, 388.5, 6.5, DIM); lab('Out', 1600, 388.5, 6.5, DIM); lab('In', 1685, 388.5, 6.5, DIM)
lab('Output', 1766, 368.5, 6.0, DIM); lab('Expander', 1766, 376.5, 6.0, DIM)
lab('Ft. Sw.', 1818, 381, 6.5, DIM); lab('Pedal • CV', 1893, 381, 6.5, DIM)
lab('Left/Mono', 1990, 381, 6.5, DIM); lab('Right', 2052, 381, 6.5, DIM)
lab('Audio In', 2110, 381, 6.5, DIM); lab('Phones', 2226, 381, 6.5, DIM)

# ---- left of the panel: the feature list under the logo
for i, t in enumerate(('16 Bit Sampling', '24 Bit Effects', 'Advanced Synthesis', '16 Track Sequencer', 'Automated Mixdown')):
    lab(t, 468, (550, 567.6, 586, 603.6, 621)[i], 6.0, GREY, 'l', 2.3)

# ---- mode buttons (legend under the button, left aligned with it)
lab('Load', 766, 537.5, 7.2, anchor='l'); lab('Command', 766, 577, 7.2, anchor='l'); lab('Edit', 766, 616.5, 7.2, anchor='l')
lab('Instrument', 838, 516.5, 7.2, anchor='l'); lab('Seq•Song', 838, 556.5, 7.2, anchor='l')
lab('System•MIDI', 838, 596, 7.2, anchor='l'); lab('Effects', 838, 635.5, 7.2, anchor='l')
# keypad: number above, page name under
COLS = (910, 953, 995)
for r, (names, ny, ty) in enumerate(((('Env 1', 'Env 2', 'Env 3'), 490.5, 516.5), (('Pitch', 'Filter', 'Amp'), 530.5, 556.5),
                                     (('LFO', 'Wave', 'Layer'), 570.5, 596))):
    for k, x in enumerate(COLS):
        lab(str(r * 3 + k + 1), x, ny, 8.0)
        lab(names[k], x, ty, 7.2)
lab('0', 953, 609.5, 8.0); lab('Track', 953, 635.5, 7.2)
# data entry
lab('No', 1135.5, 608.5, 7.2); lab('Cancel', 1135.5, 634, 7.2)
lab('Yes', 1191.5, 608.5, 7.2); lab('Enter', 1191.5, 634, 7.2)
# over the instrument buttons
lab('Loaded', 1251, 576.5, 6.6, anchor='l')
lab('Instruments • Tracks', 1419, 573, 7.6, anchor='c', sp=3.4)
lab('Selected', 1597, 574, 6.6, anchor='r')
# effects / sampling / sequencer
lab('Effect', 1641, 532.5, 7.0, anchor='l'); lab('• Select', 1645, 540.5, 6.4, anchor='l'); lab('• Bypass', 1645, 548, 6.4, anchor='l')
lab('Sample', 1727, 532.5, 7.0)
lab('Record', 1648.5, 631, 7.0); lab('Stop •', 1692.5, 631, 7.0); lab('Continue', 1692.5, 639, 7.0); lab('Play', 1734, 631, 7.0)
# section names along the bottom of the panel
lab('Volume', 705.5, 665.5, 7.4, sp=6.2); lab('Mode', 781.5, 665.5, 7.4, sp=6.2); lab('Page', 925.5, 665.5, 7.4, sp=6.2)
lab('Data Entry', 1123, 665.5, 7.4, sp=6.2)
for i in range(8): lab(str(i + 1), 1263.3 + 45.83 * i, 665.5, 7.6)
lab('Sequencer', 1720, 665.5, 7.4, sp=6.2)

# ---- left end: Patch Select and the wheels
lab('Patch Select', 204, 851, 5.4)
lab('Pitch', 240.5, 1043, 6.4, sp=5.5); lab('Mod', 318.5, 1043, 6.4, sp=5.5)

# ---- OEX-6 output expander (white print)
lab('Input', 1894.5, 456, 7.4, WHITE)
for (x0, y0, name) in ((2017, 451.5, 'Aux 1'), (2156.5, 449.5, 'Bus 2'), (2295.5, 447.5, 'Bus 3')):
    lab('Left', x0 - 22, y0, 7.0, WHITE); lab('Right', x0 + 22, y0, 7.0, WHITE)
    lab(name, x0, y0 + 13.5, 8.6, WHITE)
lab('O', 1867.5, 516, 43, WHITE, sc=False); lab('EX-6', 1917.5, 521, 30, WHITE, sc=False)
lab('Output Expander', 1903, 551, 9.0, WHITE, sp=0.6, bold=True)
lab('Wavesample / Track', 2043, 539.5, 6.0, WHITE); lab('Output', 2043, 547.5, 6.0, WHITE)
lab('Vol', 2015.5, 571.5, 5.6, WHITE); lab('Pan', 2052, 571.5, 5.8, WHITE)
lab('L', 2077, 563.5, 5.4, WHITE); lab('R', 2077, 584, 5.4, WHITE)
lab('Aux 1', 2102, 543, 6.4, WHITE, 'l'); lab('Bus 3', 2102, 553, 6.4, WHITE, 'l')
lab('Bus 2', 2102, 567.5, 6.4, WHITE, 'l'); lab('Bus 1', 2102, 582, 6.4, WHITE, 'l')
lab('FX Processor', 2234, 545.5, 6.0, WHITE)
lab('Main Stereo', 2289, 515.5, 6.2, WHITE); lab('Outputs', 2289, 523.5, 6.2, WHITE)
lab('L', 2281, 531.5, 5.0, WHITE); lab('R', 2296, 531.5, 5.0, WHITE)
lab('Volume', 2298, 556.5, 6.0, WHITE, 'l'); lab('Slider', 2298, 564.5, 6.0, WHITE, 'l')

fonts = {}
def font(px, bold=False):
    key = (px, bold)
    if key not in fonts: fonts[key] = ImageFont.truetype(BOLD if bold else FONT, px)
    return fonts[key]

alpha = bytearray()
entries = []
for t, x, y, size, col, anchor, sp, sc, bold in L:
    px_big = max(6, round(size * RES)); px_small = max(5, round(size * RES * 0.80))
    fb, fs = font(px_big, bold), font(px_small, bold)
    # small capitals: the first letter of a word is big, so are words written in capitals (MIDI,
    # LFO, FX, CV), digits and signs
    seq = []
    for wm in re.finditer(r'[^\s\u2022/]+|[\s\u2022/]', t):
        w = wm.group(0)
        allcaps = len(w) > 1 and w.replace('.', '').isupper()
        for j, ch in enumerate(w):
            big = (not sc) or allcaps or j == 0 or not ch.isalpha()
            seq.append((ch.upper(), big))
    asc_b = fb.getmetrics()[0]; asc_s = fs.getmetrics()[0]
    spacing = sp * RES
    widths = [(fb if b else fs).getlength(c) for c, b in seq]
    total = sum(widths) + spacing * (len(seq) - 1)
    cap_top = fb.getbbox('H')[1]; cap_bot = fb.getbbox('H')[3]
    W = int(total + 8); H = int(px_big * 1.35 + 6)
    im = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(im)
    base = 3 + asc_b
    cx = 4.0
    for (c, b), w in zip(seq, widths):
        f = fb if b else fs
        d.text((cx, base - (asc_b if b else asc_s)), c, font=f, fill=255, stroke_width=0)
        cx += w + spacing
    # a little heavier, like the print (and readable at the window's size); the OEX-6 print is bold
    im = Image.blend(im, im.filter(ImageFilter.MaxFilter(3)), 0.65 if col == WHITE else 0.45)
    # crop to ink (keep the cap-height centre as the anchor)
    bb = im.getbbox() or (0, 0, 1, 1)
    l, tp, r, bt = max(0, bb[0] - 2), max(0, bb[1] - 2), min(W, bb[2] + 2), min(H, bb[3] + 2)
    im = im.crop((l, tp, r, bt))
    w, h = im.size
    capc = base - asc_b + (cap_top + cap_bot) / 2 - tp      # cap centre in the cropped image (texels)
    ink_l = 4 - l                                            # left of the first glyph
    px = list(im.tobytes())
    if len(px) % 2: px.append(0)
    off = len(alpha)
    for k in range(0, len(px), 2):
        alpha.append((px[k] >> 4) | (px[k + 1] & 0xf0))
    pw, ph = w / RES, h / RES
    text_w = total / RES
    if anchor == 'c': x0 = x - text_w / 2 - ink_l / RES
    elif anchor == 'l': x0 = x - ink_l / RES
    else: x0 = x - text_w - ink_l / RES
    y0 = y - capc / RES
    entries.append((x0, y0, pw, ph, w, h, col, off))

with open(OUT, 'w') as o:
    o.write('// Generated by tools/make_kbd_art.py - do not edit.\n')
    o.write('// Legends of the EPS-16 Plus keyboard (and OEX-6), GNU FreeSans (GPL with font exception), 4-bit alpha.\n')
    o.write('#pragma once\n#include <cstdint>\n\n')
    o.write('struct KbdLegend { float x, y, w, h; uint16_t tw, th; uint8_t colour; uint32_t offset; };\n')
    o.write('static const KbdLegend KBD_LEGENDS[] = {\n')
    for (x0, y0, pw, ph, w, h, col, off), item in zip(entries, L):
        o.write('    { %.1ff, %.1ff, %.2ff, %.2ff, %d, %d, %d, %d },   // %s\n' % (x0, y0, pw, ph, w, h, col, off,
                item[0].encode('ascii', 'backslashreplace').decode()))
    o.write('};\n\nstatic const uint8_t KBD_LEGEND_ALPHA[] = {\n')
    for i in range(0, len(alpha), 24):
        o.write('    ' + ','.join('%d' % v for v in alpha[i:i + 24]) + ',\n')
    o.write('};\n')
print('%d legends, %d bytes -> %s' % (len(entries), len(alpha), OUT))
