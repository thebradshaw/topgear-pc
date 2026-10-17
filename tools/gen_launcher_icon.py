#!/usr/bin/env python3
"""Draws the launcher's icon from the Top Gear box art (not in the repo; pass its path).

Concept: the logo's gauge "O" as a target sight locked on the car's nose, with the box
art's fire behind. Large sizes show the art; 16-32 pixel sizes are drawn simply (the
ring, crosshair and fire) so they stay readable.

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


def fire_background(size, art=None):
    s = size
    if art is not None:
        # The car's nose against the fire, filling the tile.
        crop = art.crop((268, 238, 740, 562))  # 472x324
        scale = s / crop.width
        car = crop.resize((s, int(crop.height * scale)), Image.LANCZOS)
        bg = Image.new("RGB", (s, s), (0, 0, 0))
        # the fire above the car: stretch the art's fire strip at the top right
        top = s - car.height
        sky = art.crop((560, 200, 800, 300)).resize((s, top + 2), Image.LANCZOS)
        sky = sky.filter(ImageFilter.GaussianBlur(s * 0.01))
        # darken towards the top so the ring stands out
        fade = Image.linear_gradient("L").resize((s, top + 2)).point(lambda v: int(40 + v * 0.75))
        sky = Image.composite(sky, Image.new("RGB", sky.size, (0, 0, 0)), fade)
        bg.paste(sky, (0, 0))
        bg.paste(car, (0, top))
        return bg
    # simple: black with a fire glow rising from the bottom
    bg = Image.new("RGB", (s, s), (8, 6, 10))
    glow = Image.new("L", (s, s), 0)
    ImageDraw.Draw(glow).ellipse((-s * 0.3, s * 0.55, s * 1.3, s * 1.6), fill=255)
    glow = glow.filter(ImageFilter.GaussianBlur(s * 0.12))
    fire = Image.new("RGB", (s, s), (240, 110, 20))
    return Image.composite(fire, bg, glow)


def make(size, art):
    s = SS
    detailed = size >= 48
    bg = fire_background(s, art if detailed else None).convert("RGBA")
    if detailed:
        cx, cy, r, w = s * 0.5, s * 0.44, s * 0.27, s * 0.055
        ring = ring_layer(s, cx, cy, r, w)
    else:
        cx, cy, r, w = s * 0.5, s * 0.47, s * 0.33, s * (0.12 if size <= 16 else 0.10)
        ring = ring_layer(s, cx, cy, r, w, ticks=False, lines=size >= 24, line_w=int(s * 0.035))
    img = Image.alpha_composite(bg, ring)
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
