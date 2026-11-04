#!/usr/bin/env python3
"""Draws the launcher's art: the logo and the Start button's word (src/launcher_art.h) and the
launcher's icon (assets/launcher-icon.ico).

Everything is the project's own design, drawn here from the Barlow typeface in
third_party/barlow (SIL Open Font License 1.1) and plain geometry. Nothing comes from any game.

- Emblem: a globe (meridians and parallels) inside a gauge ring whose last stretch is red, with
  a needle pointing into it: the "redline" and the "atlas".
- Logo: the emblem, then the name's first word in upright Barlow Black and, under it, a rev bar
  running from steel to red and the second word in tracked Barlow SemiBold.
- Start word: START in upright Barlow Black.
- Icon: the emblem on a rounded navy tile, drawn separately for each size; the small sizes leave
  out the globe's lines.

Each picture is drawn four times finer than it is stored, then averaged down, and stored at
twice the launcher's 1280x720 layout (one texel a pixel in a 2560x1440 window). The C header
holds premultiplied RGBA, run-length packed: a byte c < 128 is followed by one pixel (4 bytes)
repeated c + 1 times; a byte c >= 128 is followed by c - 127 pixels as they are.

To rename: change NAME below (or pass --name for a preview only) and run the script again.

Usage (from the repository root; needs Pillow and numpy):
  python3 tools/gen_launcher_art.py                   writes src/launcher_art.h and the icon
  python3 tools/gen_launcher_art.py --preview DIR     also writes PNG previews to DIR
  python3 tools/gen_launcher_art.py --name "A B" --preview DIR   previews another name only
"""
import argparse
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

NAME = ("REDLINE", "ATLAS")  # the project's name: big first word, small second word

SCALE = 2  # stored texels per launcher pixel
SS = 4     # drawn this much finer again
K = SCALE * SS

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BLACK = os.path.join(ROOT, "third_party", "barlow", "Barlow-Black.ttf")
SEMIBOLD = os.path.join(ROOT, "third_party", "barlow", "Barlow-SemiBold.ttf")
CAP = 0.70  # Barlow's cap height / size (OS/2 sCapHeight 700 of 1000)

# The launcher's palette (src/launcher.c) and the emblem's colours.
RED = (255, 64, 52)
ACCENT = (255, 92, 56)
STEEL = (196, 206, 226)
STEEL_DIM = (104, 116, 148)
NAVY = (24, 28, 46)
WHITE = (255, 255, 255)


def font(path, cap):
    return ImageFont.truetype(path, int(round(cap / CAP)), layout_engine=ImageFont.Layout.BASIC)


def text_mask(size, s, f, x, baseline, track=0.0):
    """s from x on the baseline, letter by letter with the tracking. Returns the mask and the
    ink's left and right edges."""
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    for ch in s:
        d.text((x, baseline), ch, font=f, fill=255, anchor="ls")
        x += f.getlength(ch) + track
    cols = np.nonzero(np.asarray(m).max(axis=0))[0]
    return m, cols.min(), cols.max() + 1


def ramp(h, stops):
    """A vertical gradient h rows tall: stops [(row, (r, g, b)), ...] with rows increasing."""
    rows = np.arange(h, dtype=np.float32)
    return np.stack([np.interp(rows, [s[0] for s in stops], [s[1][i] for s in stops]) for i in range(3)], axis=1)


def layer(mask, colour, opacity=1.0):
    """Premultiplied RGBA from a mask (L image or 0-1 array) in a colour: an (r, g, b) tuple,
    rows x 3 (a vertical gradient) or h x w x 3."""
    a = (np.asarray(mask, np.float32) / 255.0 if isinstance(mask, Image.Image) else mask) * opacity
    c = np.empty(a.shape + (3,), np.float32)
    c[:] = colour[:, None, :] if isinstance(colour, np.ndarray) and colour.ndim == 2 else colour
    return np.dstack([c * a[..., None], a])


def over(dst, src):
    return src + dst * (1 - src[..., 3:4])


def shadow(mask, blur, dy, opacity):
    m = mask.filter(ImageFilter.GaussianBlur(blur))
    m = m.transform(m.size, Image.AFFINE, (1, 0, 0, 0, 1, -dy), resample=Image.BILINEAR)
    return layer(m, (4, 5, 12), opacity)


def emblem(acc, cx, cy, r, u, detail=True):
    """The globe in the gauge ring, centred on (cx, cy) with outer radius r; u is drawing pixels
    per launcher (or icon) pixel."""
    H, W = acc.shape[:2]

    def mask():
        return Image.new("L", (W, H), 0)

    disc = mask()
    ImageDraw.Draw(disc).ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    acc = over(acc, shadow(disc, 2.0 * u, 1.6 * u, 0.6))
    acc = over(acc, layer(disc, ramp(H, [(cy - r, (38, 44, 70)), (cy + r, (16, 18, 30))])))
    ring_w = (3.2 if detail else 4.5) * u
    ri = r - ring_w  # inside edge of the ring
    if detail:
        # Globe lines, clipped to the inside of the ring.
        g = mask()
        d = ImageDraw.Draw(g)
        lw = max(1, int(round(1.3 * u)))
        rg = ri - 2.5 * u
        for k in (0.38, 0.75):
            d.ellipse((cx - rg * k, cy - rg, cx + rg * k, cy + rg), outline=255, width=lw)
        d.line((cx, cy - rg, cx, cy + rg), fill=255, width=lw)
        for t in (-0.5, 0.0, 0.5):
            y = cy + rg * t
            half = rg * np.sqrt(1 - t * t)
            d.line((cx - half, y, cx + half, y), fill=255, width=lw)
        clip = mask()
        ImageDraw.Draw(clip).ellipse((cx - rg, cy - rg, cx + rg, cy + rg), fill=255)
        g = np.asarray(g, np.float32) / 255.0 * np.asarray(clip, np.float32) / 255.0
        acc = over(acc, layer(g, STEEL_DIM, 0.75))
    # The ring: steel round most of the way, red over the last stretch (the redline).
    ring = mask()
    ImageDraw.Draw(ring).ellipse((cx - r, cy - r, cx + r, cy + r), outline=255, width=int(round(ring_w)))
    a0, a1 = -62.0, 28.0  # the red stretch, degrees (0 = right, negative = up)
    red = mask()
    ImageDraw.Draw(red).pieslice((cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2), a0, a1, fill=255)
    ring_a = np.asarray(ring, np.float32) / 255.0
    red_a = np.asarray(red, np.float32) / 255.0
    acc = over(acc, layer(ring_a * (1 - red_a), STEEL))
    acc = over(acc, layer(ring_a * red_a, RED))
    if detail:
        # Tick marks inside the ring, the last ones red.
        t = mask()
        tr = mask()
        for i, ang in enumerate(np.linspace(-210.0, 28.0, 9)):
            rad = np.radians(ang)
            r0, r1 = ri - 1.0 * u, ri - 4.5 * u
            target = tr if ang > a0 else t
            ImageDraw.Draw(target).line((cx + r0 * np.cos(rad), cy + r0 * np.sin(rad), cx + r1 * np.cos(rad), cy + r1 * np.sin(rad)),
                                        fill=255, width=max(1, int(round(1.6 * u))))
        acc = over(acc, layer(t, STEEL, 0.9))
        acc = over(acc, layer(tr, RED))
    # The needle, pointing into the red, and its hub.
    ang = np.radians(-22.0)
    tip = (cx + (ri - (2.0 if detail else 0.5) * u) * np.cos(ang), cy + (ri - (2.0 if detail else 0.5) * u) * np.sin(ang))
    back = 0.22 * r
    half = (1.9 if detail else 2.6) * u
    nx, ny = -np.sin(ang), np.cos(ang)
    tail = (cx - back * np.cos(ang), cy - back * np.sin(ang))
    needle = mask()
    ImageDraw.Draw(needle).polygon([tip, (tail[0] + nx * half, tail[1] + ny * half), (tail[0] - nx * half, tail[1] - ny * half)], fill=255)
    acc = over(acc, shadow(needle, 0.8 * u, 0.6 * u, 0.6))
    acc = over(acc, layer(needle, WHITE))
    hub = mask()
    hr = (3.4 if detail else 4.2) * u
    ImageDraw.Draw(hub).ellipse((cx - hr, cy - hr, cx + hr, cy + hr), fill=255)
    acc = over(acc, layer(hub, RED))
    dot = mask()
    dr = hr * 0.42
    ImageDraw.Draw(dot).ellipse((cx - dr, cy - dr, cx + dr, cy + dr), fill=255)
    return over(acc, layer(dot, WHITE))


def word_face(acc, m, top, base, u):
    """Upright letters with a light steel face, a soft shadow and a fine light top edge."""
    acc = over(acc, shadow(m, 1.8 * u, 1.8 * u, 0.7))
    face = ramp(acc.shape[0], [(top, (255, 255, 255)), (top + (base - top) * 0.55, (232, 238, 248)), (base, (176, 188, 212))])
    return over(acc, layer(m, face))


def store(acc):
    """Averages the drawing down by SS and crops it to what is drawn: premultiplied RGBA8, and
    the crop's top-left corner in launcher pixels."""
    h, w = acc.shape[:2]
    acc = acc[: h // SS * SS, : w // SS * SS].reshape(h // SS, SS, w // SS, SS, 4).mean(axis=(1, 3))
    px = np.dstack([np.clip(acc[..., :3], 0, 255), np.clip(acc[..., 3:] * 255, 0, 255)]).round().astype(np.uint8)
    px[px[..., 3] == 0] = 0
    ys, xs = np.nonzero(px[..., 3])
    y0, x0 = ys.min() // SCALE * SCALE, xs.min() // SCALE * SCALE
    y1, x1 = -(-(ys.max() + 1) // SCALE) * SCALE, -(-(xs.max() + 1) // SCALE) * SCALE
    return px[y0:y1, x0:x1]


def logo(name):
    big, small = name
    W, H = 460 * K, 90 * K
    acc = np.zeros((H, W, 4), np.float32)
    r = 31 * K
    cx, cy = 6 * K + r, 8 * K + r
    acc = emblem(acc, cx, cy, r, K)
    left = cx + r + 14 * K
    top, cap = 9 * K, 36 * K
    base = top + cap
    m, ink0, ink1 = text_mask((W, H), big, font(BLACK, cap), left, base, track=1.2 * K)
    acc = word_face(acc, m, top, base, K)
    # The second word, right-aligned with the first, and the rev bar running up to it.
    sub_cap = 12 * K
    sub_base = base + 10 * K + sub_cap
    f = font(SEMIBOLD, sub_cap)
    track = 4.2 * K
    probe, s0, s1 = text_mask((W, H), small, f, 0, sub_base, track)
    sm, _, _ = text_mask((W, H), small, f, ink1 - s1, sub_base, track)
    sub_left = ink1 - (s1 - s0)
    acc = over(acc, shadow(sm, 1.2 * K, 1.0 * K, 0.6))
    acc = over(acc, layer(sm, STEEL))
    mid = sub_base - sub_cap / 2
    b0, b1 = ink0 + 1 * K, sub_left - 9 * K
    bar = Image.new("L", (W, H), 0)
    ImageDraw.Draw(bar).rectangle((b0, mid - 1.6 * K, b1, mid + 1.6 * K), fill=255)
    u = np.clip((np.arange(W, dtype=np.float32) - b0) / max(1.0, b1 - b0), 0, 1)
    k = np.clip((u - 0.55) / 0.3, 0, 1)[:, None]  # steel, warming to red over the last part
    colours = np.array(STEEL_DIM, np.float32) * (1 - k) + np.array(RED, np.float32) * k
    acc = over(acc, layer(bar, colours[None, :, :].repeat(H, axis=0)))
    return store(acc)


def start_word():
    W, H = 220 * K, 56 * K
    acc = np.zeros((H, W, 4), np.float32)
    top, cap = 10 * K, 27 * K
    m, _, _ = text_mask((W, H), "START", font(BLACK, cap), 10 * K, top + cap, track=4.0 * K)
    return store(word_face(acc, m, top, top + cap, K))


def icon(size):
    """The icon, size x size, straight (not premultiplied) RGBA."""
    ss = 8
    S = size * ss
    u = S / 64.0  # drawing pixels per pixel of a 64-pixel icon
    acc = np.zeros((S, S, 4), np.float32)
    tile = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tile).rounded_rectangle((0, 0, S - 1, S - 1), radius=S * 0.2, fill=255)
    acc = over(acc, layer(tile, ramp(S, [(0, (44, 50, 80)), (S, (14, 16, 26))])))
    detail = size >= 48
    r = S * (0.40 if detail else 0.44)
    acc = emblem(acc, S / 2, S / 2, r, u * (1.0 if detail else 1.6), detail=detail)
    acc = acc.reshape(size, ss, size, ss, 4).mean(axis=(1, 3))
    a = acc[..., 3:]
    rgb = np.where(a > 1e-4, acc[..., :3] / np.maximum(a, 1e-4), 0)
    return Image.fromarray(np.dstack([rgb.clip(0, 255), (a * 255).clip(0, 255)]).round().astype(np.uint8), "RGBA")


def pack(px):
    """Run-length packing (see the top of this file)."""
    p = px.reshape(-1, 4)
    out = bytearray()
    lit = []

    def flush():
        while lit:
            n = min(128, len(lit))
            out.append(127 + n)
            for q in lit[:n]:
                out.extend(q)
            del lit[:n]

    i = 0
    while i < len(p):
        run = 1
        while i + run < len(p) and run < 128 and (p[i + run] == p[i]).all():
            run += 1
        if run >= 2:
            flush()
            out.append(run - 1)
            out += bytes(p[i])
        else:
            lit.append(bytes(p[i]))
        i += run
    flush()
    return bytes(out)


def unpack(data, n):
    out = bytearray()
    i = 0
    while i < len(data):
        c = data[i]
        i += 1
        if c < 128:
            out += data[i:i + 4] * (c + 1)
            i += 4
        else:
            out += data[i:i + 4 * (c - 127)]
            i += 4 * (c - 127)
    assert len(out) == n * 4
    return bytes(out)


def c_array(name, data):
    lines = ["static const unsigned char %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 40):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 40]) + ",")
    lines.append("};")
    return lines


def straight(px):
    """Premultiplied texels as a straight-alpha image."""
    a = px[..., 3:].astype(np.float32)
    rgb = np.where(a > 0, px[..., :3] * 255.0 / np.maximum(a, 1), 0)
    return Image.fromarray(np.dstack([rgb.round().clip(0, 255), a]).astype(np.uint8), "RGBA")


def header_preview(art, scale):
    """The logo over the launcher's header as src/launcher.c draws it (the road left out)."""
    w, h = 980, 92
    bg = Image.new("RGBA", (w * scale, h * scale), (14, 16, 24, 255))
    d = ImageDraw.Draw(bg)
    for y in range(86):
        d.rectangle((0, y * scale, w * scale, (y + 1) * scale - 1), fill=(20 + y // 4, 22 + y // 6, 36 + y // 3))
    d.rectangle((0, 86 * scale, w * scale, 88 * scale - 1), fill=ACCENT)
    im = straight(art)
    if scale != SCALE:
        im = im.resize((im.width * scale // SCALE, im.height * scale // SCALE), Image.LANCZOS)
    y = (86 * scale - im.height) // 2
    bg.alpha_composite(im, (40 * scale - 6 * scale, y))
    return bg


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", metavar="DIR", help="also write PNG previews to DIR")
    ap.add_argument("--name", help='preview another name, e.g. "Race Masters" (writes no source files)')
    args = ap.parse_args()
    name = NAME
    if args.name:
        words = args.name.upper().split()
        name = (" ".join(words[:-1]) or words[0], words[-1] if len(words) > 1 else "")
    logo_px = logo(name)
    start_px = start_word()
    if not args.name:
        lines = ["// Generated by tools/gen_launcher_art.py: the launcher's logo and the Start button's word,",
                 "// drawn from Barlow (SIL Open Font License 1.1, third_party/barlow) and geometry. Do not",
                 "// edit. Premultiplied RGBA at LAUNCHER_ART_SCALE texels a launcher pixel, run-length",
                 "// packed: a byte c < 128 is followed by one pixel (4 bytes) repeated c + 1 times; a byte",
                 "// c >= 128 is followed by c - 127 pixels as they are.",
                 "#define LAUNCHER_ART_SCALE %d" % SCALE,
                 "#define LOGO_ART_W %d" % logo_px.shape[1], "#define LOGO_ART_H %d" % logo_px.shape[0],
                 "#define START_ART_W %d" % start_px.shape[1], "#define START_ART_H %d" % start_px.shape[0]]
        for cname, px in (("kLogoArt", logo_px), ("kStartArt", start_px)):
            data = pack(px)
            assert unpack(data, px.shape[0] * px.shape[1]) == px.tobytes()
            lines += c_array(cname, data)
            print("%s %dx%d: %d bytes packed (%d raw)" % (cname, px.shape[1], px.shape[0], len(data), px.size))
        with open(os.path.join(ROOT, "src", "launcher_art.h"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        sizes = [16, 24, 32, 48, 64, 128, 256]
        icons = [icon(n) for n in sizes]
        icons[-1].save(os.path.join(ROOT, "assets", "launcher-icon.ico"), format="ICO",
                       sizes=[(n, n) for n in sizes], append_images=icons[:-1])
    if args.preview:
        out = args.preview
        os.makedirs(out, exist_ok=True)
        tag = "" if not args.name else "-" + "-".join(args.name.lower().split())
        for s in (1, 2):
            header_preview(logo_px, s).save(os.path.join(out, "logo%s_header_%dx.png" % (tag, s)))
        straight(logo_px).save(os.path.join(out, "logo%s.png" % tag))
        if not args.name:
            straight(start_px).save(os.path.join(out, "start.png"))
            sizes = [16, 24, 32, 48, 64, 128, 256]
            sheet = Image.new("RGBA", (sum(sizes) + 20 * len(sizes) + 20, 296), (40, 40, 48, 255))
            x = 20
            for n in sizes:
                sheet.alpha_composite(icon(n), (x, 276 - n))
                x += n + 20
            sheet.save(os.path.join(out, "icon_sizes.png"))


if __name__ == "__main__":
    main()
