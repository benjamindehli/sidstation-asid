#!/usr/bin/env python3
"""Generate the Open Graph / social cards under docs/.

Three cards are written, all with the same wording and palette:

    og-cover.png       1200x630   16:9, used for Open Graph and Twitter
    og-cover-4x3.png   1200x900   4:3
    og-cover-1x1.png   1200x1200  1:1

Google picks between aspect ratios when it builds an article rich result, so all
three are listed in each page's JSON-LD. The taller two are laid out from
scratch rather than cropped from the 16:9 card, which would cut the text.

The site uses the PETSCII pixel font (asid/assets/SidStationC64.ttf). That font
builds each glyph from one separate rectangle per pixel row, so letting FreeType
rasterize it leaves faint anti-aliased seams where the rectangles abut (they show
up as a gap under serifs like the "I"). To avoid that, this script reads each
glyph's outline, samples it to the 8x8 pixel grid itself, and draws solid blocks,
so the text is pixel-perfect with no seams, no anti-aliasing and no JPEG ringing.

Run from anywhere:  python3 packaging/make-og.py
Needs:              pip install Pillow fonttools

Edit TEXT below to change the wording, palette or layout, then re-run.
"""

from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "asid/assets/SidStationC64.ttf"
ICON = ROOT / "asid/assets/AppIcon.png"
OUT_DIR = ROOT / "docs"

SCREEN = (70, 62, 164)  # SID screen blue (matches the site --screen)
WHITE = (255, 255, 255)
TEAL = (60, 184, 166)
LIGHT = (183, 179, 238)
DIM = (150, 145, 205)

# (text, scale = output pixels per font pixel, colour) for the 16:9 card, with
# the top-left position it is pasted at.
TEXT = [
    ("SIDSTATION", 10, WHITE, (70, 150)),
    ("ASID", 10, TEAL, (70, 258)),
    ("CONTROL THE THREE ELEKTRON SIDSTATION", 3, LIGHT, (72, 408)),
    ("VOICES INDIVIDUALLY FROM YOUR DAW", 3, LIGHT, (72, 444)),
    ("DEHLI MUSIKK  /  VST3 . AU . STANDALONE", 2, DIM, (72, 566)),
]

# The taller cards use the same lines centred in a single column under the icon.
# (text, scale, colour, gap in pixels above this line)
STACK = [
    ("SIDSTATION", 10, WHITE, 44),
    ("ASID", 10, TEAL, 38),
    ("CONTROL THE THREE ELEKTRON SIDSTATION", 3, LIGHT, 80),
    ("VOICES INDIVIDUALLY FROM YOUR DAW", 3, LIGHT, 15),
]
FOOTER = ("DEHLI MUSIKK  /  VST3 . AU . STANDALONE", 2, DIM)
FOOTER_MARGIN = 50  # from the footer baseline row to the bottom edge

# The font is on an 800-unit em, 100 units per pixel. Sample cell centres: 8
# columns across, and the 7 rows the glyphs occupy (top y 0..700), top to bottom.
YROWS = [650, 550, 450, 350, 250, 150, 50]
XCOLS = [50, 150, 250, 350, 450, 550, 650, 750]

_font = TTFont(FONT)
_cmap = _font.getBestCmap()
_glyf = _font["glyf"]
_cache = {}


def bitmap(ch):
    """Sample a glyph's outline to a 7x8 grid of filled cells."""
    if ch in _cache:
        return _cache[ch]
    gid = _cmap.get(ord(ch))
    grid = [[False] * 8 for _ in YROWS]
    gl = _glyf[gid] if gid is not None else None
    if gl is not None and gl.numberOfContours > 0:
        coords = list(gl.coordinates)
        ends = list(gl.endPtsOfContours)

        def inside(cx, cy):
            crossings = 0
            start = 0
            for end in ends:
                pts = coords[start : end + 1]
                n = len(pts)
                for i in range(n):
                    x1, y1 = pts[i]
                    x2, y2 = pts[(i + 1) % n]
                    if (y1 > cy) != (y2 > cy):
                        xint = x1 + (cy - y1) * (x2 - x1) / (y2 - y1)
                        if cx < xint:
                            crossings += 1
                start = end + 1
            return crossings % 2 == 1

        grid = [[inside(x, y) for x in XCOLS] for y in YROWS]
    _cache[ch] = grid
    return grid


def text_layer(text, scale, color):
    layer = Image.new("RGBA", (len(text) * 8 * scale, 7 * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    for ci, ch in enumerate(text):
        ox = ci * 8 * scale
        for r, row in enumerate(bitmap(ch)):
            for c, on in enumerate(row):
                if on:
                    x0, y0 = ox + c * scale, r * scale
                    draw.rectangle([x0, y0, x0 + scale - 1, y0 + scale - 1], fill=(*color, 255))
    return layer


def icon_layer(size):
    return Image.open(ICON).convert("RGBA").resize((size, size), Image.LANCZOS)


def wide_card():
    """The 16:9 card: text ranged left, icon in the top right corner."""
    card = Image.new("RGB", (1200, 630), SCREEN)
    icon = icon_layer(250)
    card.paste(icon, (890, 74), icon)
    for text, scale, color, pos in TEXT:
        layer = text_layer(text, scale, color)
        card.paste(layer, pos, layer)
    return card


def stacked_card(height, icon_size):
    """A taller card: one centred column, icon on top, footer along the bottom."""
    card = Image.new("RGB", (1200, height), SCREEN)
    icon = icon_layer(icon_size)
    layers = [(text_layer(t, s, c), gap) for t, s, c, gap in STACK]

    foot = text_layer(*FOOTER)
    foot_top = height - FOOTER_MARGIN - foot.height
    stack_h = icon_size + sum(layer.height + gap for layer, gap in layers)
    y = (foot_top - stack_h) // 2

    card.paste(icon, ((1200 - icon_size) // 2, y), icon)
    y += icon_size
    for layer, gap in layers:
        y += gap
        card.paste(layer, ((1200 - layer.width) // 2, y), layer)
        y += layer.height
    card.paste(foot, ((1200 - foot.width) // 2, foot_top), foot)
    return card


def main():
    cards = [
        ("og-cover.png", wide_card()),
        ("og-cover-4x3.png", stacked_card(900, 260)),
        ("og-cover-1x1.png", stacked_card(1200, 320)),
    ]
    for name, card in cards:
        out = OUT_DIR / name
        card.save(out, "PNG")
        print(f"wrote {out} ({card.width}x{card.height}, {out.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
