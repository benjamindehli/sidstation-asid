#!/usr/bin/env python3
"""Build asid/assets/SidStationC64.ttf from the PETSCII 8x8 bitmap sheet.

The sheet (asid/tools/characters.png) is 16 glyphs wide x 8 tall, each an 8x8
cell, black-on-white, in C64 screen-code order:
  0 = '@', 1..26 = 'A'..'Z', 27 = '[', 29 = ']', 32..63 == ASCII 32..63.
Each lit pixel becomes a filled square; horizontal runs are merged per row to
keep the contour count down. Lowercase is aliased to the uppercase glyphs (this
charset has no lowercase). The left side bearing of each glyph is set to its
xMin, otherwise renderers shift a centred glyph (e.g. "I", whose xMin is larger
than the letters around it) to the left.

Run from anywhere:  python3 asid/tools/make_font.py
Requires:  pip install pillow fonttools
"""

import os

from PIL import Image
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHEET = os.path.join(ROOT, "asid", "tools", "characters.png")
OUT = os.path.join(ROOT, "asid", "assets", "SidStationC64.ttf")

PX = 100  # font units per source pixel
CELL = 8  # 8x8 source grid
EM = PX * CELL  # 800 units per em
ASCENT = PX * 7  # 7 px above the baseline
DESCENT = PX * 1  # 1 px below (the bottom grid row)
ADVANCE = PX * CELL  # monospaced: the full 8 px cell


def codepoint_map():
    """Screen code -> Unicode codepoint for the printable subset we use."""
    m = {0: ord("@")}
    for i in range(1, 27):
        m[i] = ord("A") + (i - 1)
    m[27] = ord("[")
    m[29] = ord("]")
    for code in range(32, 64):  # space .. '?' share ASCII values
        m[code] = code
    return m


def cell_bits(img, code):
    """The 8x8 on/off bitmap for a screen code (True = lit / black)."""
    col, row = code % 16, code // 16
    ox, oy = col * CELL, row * CELL
    px = img.load()
    return [[px[ox + gx, oy + gy] < 128 for gx in range(CELL)] for gy in range(CELL)]


def glyph_from_bits(bits):
    """A TrueType glyph: each row's lit runs become one rectangle contour.

    Grid row gy (0 top .. 7 bottom) maps so the top 7 rows sit above the
    baseline and the bottom row hangs one pixel below it.
    """
    pen = TTGlyphPen(None)
    for gy in range(CELL):
        y_top = ASCENT - gy * PX
        y_bot = y_top - PX
        gx = 0
        while gx < CELL:
            if not bits[gy][gx]:
                gx += 1
                continue
            x0 = gx
            while gx < CELL and bits[gy][gx]:
                gx += 1
            pen.moveTo((x0 * PX, y_bot))
            pen.lineTo((x0 * PX, y_top))
            pen.lineTo((gx * PX, y_top))
            pen.lineTo((gx * PX, y_bot))
            pen.closePath()
    return pen.glyph()


def notdef_glyph():
    """A filled box so a missing character is visible rather than blank."""
    pen = TTGlyphPen(None)
    x0, y0, x1, y1 = PX, -DESCENT + PX, ADVANCE - PX, ASCENT - PX
    pen.moveTo((x0, y0))
    pen.lineTo((x0, y1))
    pen.lineTo((x1, y1))
    pen.lineTo((x1, y0))
    pen.closePath()
    return pen.glyph()


def main():
    img = Image.open(SHEET).convert("L")
    assert img.size == (128, 64), f"unexpected sheet size {img.size}"

    glyphs = {".notdef": notdef_glyph()}
    order = [".notdef"]
    cmap = {}
    for code, cp in codepoint_map().items():
        name = f"u{cp:04X}"
        if name not in glyphs:
            glyphs[name] = glyph_from_bits(cell_bits(img, code))
            order.append(name)
        cmap[cp] = name
        if ord("A") <= cp <= ord("Z"):
            cmap[cp + 32] = name  # lowercase -> uppercase glyph

    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)

    # Left side bearing must equal each glyph's xMin, or the renderer shifts a
    # centred glyph (e.g. "I") left to align its edge with lsb=0.
    glyf = fb.font["glyf"]
    metrics = {}
    for name in order:
        g = glyf[name]
        g.recalcBounds(glyf)
        xmin = getattr(g, "xMin", 0) if g.numberOfContours != 0 else 0
        metrics[name] = (ADVANCE, xmin)
    fb.setupHorizontalMetrics(metrics)

    fb.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    fb.setupNameTable(
        {
            "familyName": "SidStation C64",
            "styleName": "Regular",
            "psName": "SidStationC64-Regular",
            "fullName": "SidStation C64",
            "version": "Version 1.1",
            "copyright": "PETSCII 8x8 bitmap redrawn for SidStation ASID",
        }
    )
    fb.setupOS2(
        sTypoAscender=ASCENT, sTypoDescender=-DESCENT, usWinAscent=ASCENT, usWinDescent=DESCENT
    )
    fb.setupPost()
    fb.save(OUT)

    # Fixed head timestamps so regenerating an unchanged sheet is byte-stable
    # (fontTools otherwise stamps the current time, churning the committed TTF).
    out = TTFont(OUT)
    out.recalcTimestamp = False
    out["head"].created = 0
    out["head"].modified = 0
    out.save(OUT)
    print(f"wrote {OUT}: {len(order)} glyphs, {len(cmap)} codepoints")


if __name__ == "__main__":
    main()
