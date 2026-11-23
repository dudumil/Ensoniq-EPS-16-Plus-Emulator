#!/usr/bin/env python3
"""Installer icon: the app icon (src/mac/<name>_icon.png, 1024 px) with a download badge (a round
badge with an arrow into a tray) at the bottom right.
Outputs installer/icons/setup.png (1024), setup.ico (Windows, 16-256) and setup.icns (macOS).
Usage: python3 tools/make_installer_icon.py <name>      (eps16 / asr10)"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
name = sys.argv[1]
S = 1024
icon = Image.open(os.path.join(ROOT, 'src', 'mac', name + '_icon.png')).convert('RGBA').resize((S, S), Image.LANCZOS)

def badge(scale):
    """the badge drawn at `scale` times its size (supersampled), as an RGBA layer of S x S"""
    B = S * scale
    layer = Image.new('RGBA', (B, B), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy, r = int(0.735 * B), int(0.735 * B), int(0.235 * B)
    # shadow
    sh = Image.new('RGBA', (B, B), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse((cx - r, cy - r + int(0.012 * B), cx + r, cy + r + int(0.012 * B)), fill=(0, 0, 0, 150))
    sh = sh.filter(ImageFilter.GaussianBlur(0.018 * B))
    layer = Image.alpha_composite(layer, sh)
    d = ImageDraw.Draw(layer)
    # white ring, then the green-blue disc with a soft vertical gradient
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255, 255))
    ri = int(r * 0.88)
    disc = Image.new('RGBA', (B, B), (0, 0, 0, 0))
    dd = ImageDraw.Draw(disc)
    for y in range(cy - ri, cy + ri + 1):
        t = (y - (cy - ri)) / (2 * ri)
        c = (int(40 + 10 * t), int(170 - 60 * t), int(255 - 70 * t), 255)
        dd.line([(cx - ri, y), (cx + ri, y)], fill=c)
    m = Image.new('L', (B, B), 0)
    ImageDraw.Draw(m).ellipse((cx - ri, cy - ri, cx + ri, cy + ri), fill=255)
    layer.paste(disc, (0, 0), m)
    d = ImageDraw.Draw(layer)
    # arrow down into a tray
    w = int(0.075 * r * 1.0)
    sw = int(r * 0.14)                      # shaft width
    top, tip = cy - int(r * 0.52), cy + int(r * 0.22)
    head = int(r * 0.38)
    d.rectangle((cx - sw // 2, top, cx + sw // 2, tip - head // 2), fill='white')
    d.polygon([(cx - head, tip - head), (cx + head, tip - head), (cx, tip + int(head * 0.15))], fill='white')
    ty = cy + int(r * 0.42); tw = int(r * 0.56); th = int(r * 0.12)
    d.rounded_rectangle((cx - tw, ty, cx + tw, ty + th), radius=th // 2, fill='white')
    d.rounded_rectangle((cx - tw, ty - int(r * 0.22), cx - tw + th, ty + th), radius=th // 2, fill='white')
    d.rounded_rectangle((cx + tw - th, ty - int(r * 0.22), cx + tw, ty + th), radius=th // 2, fill='white')
    return layer.resize((S, S), Image.LANCZOS)

out = Image.alpha_composite(icon, badge(3))
od = os.path.join(ROOT, 'installer', 'icons')
os.makedirs(od, exist_ok=True)
out.save(os.path.join(od, 'setup.png'))
out.save(os.path.join(od, 'setup.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
out.save(os.path.join(od, 'setup.icns'))
print('installer/icons/setup.png, setup.ico, setup.icns')
