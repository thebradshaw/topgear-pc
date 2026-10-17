#!/usr/bin/env python3
"""Draws the launcher's icon (assets/launcher-icon.ico) from the Top Gear box art (pass its path).

The car's nose against the box art's fire, on a rounded tile.

Usage: gen_launcher_icon.py box-art.png out.ico [preview.png]
"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

SS = 1024  # drawing size; every size is scaled from its own drawing

GOLD_HI = (255, 214, 92)
GOLD = (246, 150, 30)
GOLD_LO = (178, 70, 12)
LILAC = (150, 140, 230)
RED = (226, 40, 30)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size - 1, size - 1), radius, fill=255)
    return m


def gold_fill(size):
    """A vertical gold ramp, the logo's fiery orange."""
    g = Image.new("RGB", (1, 256))
    for y in range(256):
        t = y / 255
        if t < 0.5:
            a, b, u = GOLD_HI, GOLD, t / 0.5
        else:
            a, b, u = GOLD, GOLD_LO, (t - 0.5) / 0.5
        g.putpixel((0, y), tuple(int(a[i] + (b[i] - a[i]) * u) for i in range(3)))
    return g.resize((size, size))


def ring_layer(size, cx, cy, r, w, ticks=True, lines=True, line_w=None):
    """The gauge sight: gold ring, red redline arc, tick marks, crosshair."""
    s = size
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    lw = line_w or max(1, int(w * 0.18))
    if lines:  # drafting crosshair, through and past the ring
        ext = r * 1.75
        d.line((cx - ext, cy, cx + ext, cy), fill=LILAC + (230,), width=lw)
        d.line((cx, cy - ext, cx, cy + ext), fill=LILAC + (230,), width=lw)
        dot = lw * 2.2
        for px, py in ((cx - ext, cy), (cx + ext, cy), (cx, cy - ext)):
            d.ellipse((px - dot, py - dot, px + dot, py + dot), fill=LILAC + (255,))
    # shadow under the ring
    sh = Image.new("L", (s, s), 0)
    ImageDraw.Draw(sh).ellipse((cx - r - w * 0.2, cy - r - w * 0.2, cx + r + w * 0.2, cy + r + w * 0.2), outline=255, width=int(w * 1.4))
    sh = sh.filter(ImageFilter.GaussianBlur(w * 0.35))
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    shadow.putalpha(sh.point(lambda v: v * 0.75))
    layer = Image.alpha_composite(shadow, layer)
    # gold ring
    m = Image.new("L", (s, s), 0)
    ImageDraw.Draw(m).ellipse((cx - r, cy - r, cx + r, cy + r), outline=255, width=int(w))
    gold = gold_fill(s).convert("RGBA")
    gold.putalpha(m)
    layer = Image.alpha_composite(layer, gold)
    d = ImageDraw.Draw(layer)
    ri = r - w  # inside edge
    # redline arc, just inside the ring at the top right like the logo's O
    aw = max(1, int(w * 0.45))
    rr = ri - aw * 0.9
    d.arc((cx - rr, cy - rr, cx + rr, cy + rr), -75, 15, fill=RED + (255,), width=aw)
    if ticks:
        for a in range(-160, -80, 16):
            t = math.radians(a)
            r0, r1 = ri - w * 0.2, ri - w * 0.9
            d.line((cx + r0 * math.cos(t), cy + r0 * math.sin(t), cx + r1 * math.cos(t), cy + r1 * math.sin(t)),
                   fill=GOLD_HI + (255,), width=max(1, int(w * 0.28)))
    # centre pip
    p = max(1.0, w * 0.35)
    d.ellipse((cx - p, cy - p, cx + p, cy + p), outline=LILAC + (255,), width=max(1, int(p * 0.5)))
    return layer


def car_tile(size, art):
    """The car's nose, a wheel and the fire, cropped square from the box art."""
    a = art.copy()
    # the poster's fold creases run down two columns here; fill them from their neighbours
    px = a.load()
    for left, right in ((263, 268), (531, 535)):
        for y in range(a.height):
            l, r = px[left, y], px[right, y]
            for x in range(left + 1, right):
                t = (x - left) / (right - left)
                px[x, y] = tuple(int(l[i] + (r[i] - l[i]) * t) for i in range(3))
    return a.crop((220, 200, 600, 580)).resize((size, size), Image.LANCZOS)


def make(size, art):
    s = SS
    img = car_tile(s, art).convert("RGBA")
    # thin dark-gold edge
    edge = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    rad = int(s * 0.18)
    ImageDraw.Draw(edge).rounded_rectangle((0, 0, s - 1, s - 1), rad, outline=(90, 40, 10, 255), width=int(s * 0.018))
    img = Image.alpha_composite(img, edge)
    img.putalpha(ImageChops.multiply(img.getchannel("A"), rounded_mask(s, rad)))
    return img.resize((size, size), Image.LANCZOS)


def main():
    art = Image.open(sys.argv[1]).convert("RGB")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = [make(n, art) for n in sizes]
    imgs[-1].save(sys.argv[2], format="ICO", sizes=[(n, n) for n in sizes], append_images=imgs[:-1])
    if len(sys.argv) > 3:
        w = sum(sizes) + 20 * len(sizes) + 20
        sheet = Image.new("RGBA", (w, 296), (40, 40, 48, 255))
        x = 20
        for n, im in zip(sizes, imgs):
            sheet.alpha_composite(im, (x, 276 - n))
            x += n + 20
        sheet.save(sys.argv[3])


if __name__ == "__main__":
    main()
