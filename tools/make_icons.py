#!/usr/bin/env python3
"""Application icons for the EPS-16 Plus and ASR-10 emulators.
Inputs: tools/icon_vfd_eps.png / icon_vfd_asr.png = renders of the emulator's VFD at scale 3
(tools/vfdshot.cpp: vfdshot "EPS-16+" 0 out.bmp). Outputs (per model): <name>_icon.png (1024),
<name>.ico (Windows, 16-256), <name>.icns (macOS) and <name>_icon64.h (window icon, RGBA)."""
import sys, os, io, struct
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
S = 1024

def vfd_text(png, n):
    im = Image.open(png).convert('RGB')
    return im.crop((28, 168, 28 + 78 * n + 6, 272))

def make(model):
    eps = model == 'eps'
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    body = Image.new('RGBA', (S, S)); d = ImageDraw.Draw(body)
    for y in range(S):
        t = y / S
        c = (int(36 - 22 * t), int(37 - 22 * t), int(40 - 22 * t)) if eps else (int(60 - 26 * t), int(61 - 26 * t), int(65 - 26 * t))
        d.line([(0, y), (S, y)], fill=c + (255,))
    # brushed metal
    import random; rnd = random.Random(7)
    for y in range(0, S, 2):
        v = rnd.randint(-5, 5)
        if v: d.line([(0, y), (S, y)], fill=(0, 0, 0, 0) if False else None) if False else None
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((96, 96, S - 96, S - 96), radius=190, fill=255)
    icon.paste(body, (0, 0), mask)
    g = ImageDraw.Draw(icon)
    # bevel: light top edge
    g.rounded_rectangle((96, 96, S - 96, S - 96), radius=190, outline=(90, 92, 98, 255) if not eps else (70, 70, 76, 255), width=4)
    # display window
    win = (160, 300, S - 160, 640)
    g.rounded_rectangle((win[0] - 8, win[1] - 8, win[2] + 8, win[3] + 8), radius=40, fill=(48, 49, 53, 255))
    g.rounded_rectangle(win, radius=34, fill=(4, 6, 7, 255))
    txt = vfd_text(os.path.join(HERE, 'icon_vfd_%s.png' % model), 7 if eps else 6)
    k = (win[2] - win[0] - 70) / txt.width
    txt = txt.resize((int(txt.width * k), int(txt.height * k)), Image.LANCZOS)
    glow = txt.filter(ImageFilter.GaussianBlur(9))
    x0 = (S - txt.width) // 2 + 8; y0 = (win[1] + win[3]) // 2 - txt.height // 2
    icon.alpha_composite(Image.merge('RGBA', (*glow.split(), Image.new('L', glow.size, 150))), (x0, y0))
    tm = Image.eval(txt.convert('L'), lambda v: 255 if v > 14 else v * 18)
    icon.paste(txt, (x0, y0), tm)
    # reflection on the glass
    refl = Image.new('RGBA', (S, S), (0, 0, 0, 0)); rd = ImageDraw.Draw(refl)
    rd.rounded_rectangle((win[0], win[1], win[2], (win[1] + win[3]) // 2), radius=34, fill=(255, 255, 255, 10))
    icon.alpha_composite(refl)
    if eps:
        # purple rules (like PAGE / DATA ENTRY) and light grey keys with amber / red LEDs
        g.rectangle((170, 690, 400, 697), fill=(108, 58, 114, 255)); g.rectangle((624, 690, S - 170, 697), fill=(108, 58, 114, 255))
        for i, (col, led) in enumerate([((112, 114, 118), (255, 160, 32)), ((112, 114, 118), None), ((112, 114, 118), (255, 42, 26)), ((40, 41, 44), None)]):
            bx = 250 + i * 135
            if led: g.rectangle((bx + 30, 735, bx + 54, 750), fill=led + (255,))
            else: g.rectangle((bx + 30, 735, bx + 54, 750), fill=(60, 24, 18, 255))
            g.rounded_rectangle((bx, 770, bx + 84, 826), radius=8, fill=(6, 6, 7, 255))
            g.rounded_rectangle((bx + 3, 772, bx + 81, 820), radius=7, fill=col + (255,))
            g.line((bx + 8, 774, bx + 76, 774), fill=tuple(min(255, c + 34) for c in col) + (255,), width=3)
    else:
        # blue legend rule (like "Mode") and dark keys with red / yellow LEDs
        g.rectangle((170, 690, 430, 696), fill=(63, 155, 208, 255)); g.rectangle((594, 690, S - 170, 696), fill=(63, 155, 208, 255))
        for i, led in enumerate([(255, 42, 26), (255, 192, 32), None, (255, 42, 26), None]):
            bx = 235 + i * 115
            g.rectangle((bx + 28, 735, bx + 50, 749), fill=(led or (60, 22, 16)) + (255,))
            g.rounded_rectangle((bx, 770, bx + 80, 826), radius=8, fill=(6, 6, 7, 255))
            g.rounded_rectangle((bx + 3, 772, bx + 77, 820), radius=7, fill=(44, 45, 49, 255))
            g.line((bx + 8, 774, bx + 72, 774), fill=(78, 80, 86, 255), width=3)
    # LED glow
    gl = icon.filter(ImageFilter.GaussianBlur(14))
    return icon

def write_all(model, name, outdir):
    icon = make(model)
    os.makedirs(outdir, exist_ok=True)
    icon.save(os.path.join(outdir, name + '_icon.png'))
    icon.save(os.path.join(outdir, name + '.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    entries = [(b'ic07', 128), (b'ic08', 256), (b'ic09', 512), (b'ic10', 1024), (b'ic11', 32), (b'ic12', 64), (b'ic13', 256), (b'ic14', 512)]
    chunks = b''
    for tag, size in entries:
        buf = io.BytesIO(); icon.resize((size, size), Image.LANCZOS).save(buf, 'PNG'); data = buf.getvalue()
        chunks += tag + struct.pack('>I', len(data) + 8) + data
    open(os.path.join(outdir, name + '.icns'), 'wb').write(b'icns' + struct.pack('>I', len(chunks) + 8) + chunks)
    small = icon.resize((64, 64), Image.LANCZOS)
    px = small.tobytes()
    with open(os.path.join(outdir, name + '_icon64.h'), 'w') as f:
        f.write('// Generated by tools/make_icons.py: 64x64 RGBA window icon\n#pragma once\n#include <cstdint>\n')
        f.write('static const uint8_t APP_ICON64[64 * 64 * 4] = {\n')
        for i in range(0, len(px), 32): f.write('    ' + ','.join(str(b) for b in px[i:i + 32]) + ',\n')
        f.write('};\n')
    print('ok', name)

if __name__ == '__main__':
    model, name, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    write_all(model, name, outdir)
