#!/usr/bin/env python3
"""
bitmap_to_ttf.py - Turn the generated fbcons bitmap font into a monospace TTF.

Reads the C header written by ttf_to_bitmap.py (the final glyph grids, with
hand-drawn overrides and centring already applied) and builds a TrueType font
whose glyphs are unions of square pixels.

    pip install fonttools
    python3 scripts/bitmap_to_ttf.py --header user/services/fbcons/font16x16.h \
        --out scripts/zuzufont12-mono.ttf

One pixel is PX font units (64), so a 12x16 cell is a 768-unit advance on a
1024-unit em: render it at 16 ppem and every design pixel lands on one pixel.
"""

import argparse
import re
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

PX = 64
UPEM = 1024
PANOSE_MONO = dict(bFamilyType=2, bSerifStyle=0, bWeight=5, bProportion=9, bContrast=0,
                   bStrokeVariation=0, bArmStyle=0, bLetterForm=0, bMidline=0, bXHeight=0)


def parse_header(path: str):
    text = open(path).read()
    def macro(name):
        return int(re.search(rf"#define\s+{name}\s+(\d+)", text).group(1))
    first, last = macro("FONT_FIRST_CHAR"), macro("FONT_LAST_CHAR")
    w, h = macro("FONT_GLYPH_W"), macro("FONT_GLYPH_H")
    rows = [[int(v, 16) for v in m.group(1).split(",")]
            for m in re.finditer(r"\{\s*((?:0x[0-9A-Fa-f]+\s*,?\s*)+)\}\s*,", text)]
    if len(rows) != last - first + 1 or any(len(r) != h for r in rows):
        sys.exit(f"{path}: expected {last - first + 1} glyphs of {h} rows")
    return first, w, h, rows


def rects(rows, w, h):
    """Merge pixels into rectangles: horizontal runs, extended downwards."""
    grid = [[bool(r & (1 << (w - 1 - x))) for x in range(w)] for r in rows]
    used = [[False] * w for _ in range(h)]
    out = []
    for y in range(h):
        x = 0
        while x < w:
            if not grid[y][x] or used[y][x]:
                x += 1
                continue
            x1 = x
            while x1 + 1 < w and grid[y][x1 + 1] and not used[y][x1 + 1]:
                x1 += 1
            y1 = y
            while y1 + 1 < h and all(grid[y1 + 1][i] and not used[y1 + 1][i]
                                     for i in range(x, x1 + 1)):
                y1 += 1
            for yy in range(y, y1 + 1):
                for xx in range(x, x1 + 1):
                    used[yy][xx] = True
            out.append((x, y, x1 + 1, y1 + 1))
            x = x1 + 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--header", required=True)
    ap.add_argument("--out", default="zuzufont12-mono.ttf")
    ap.add_argument("--family", default="zuzufont12 Mono")
    ap.add_argument("--ascent-px", type=int, default=12)
    args = ap.parse_args()

    first, w, h, glyph_rows = parse_header(args.header)
    asc = args.ascent_px * PX
    desc = (h - args.ascent_px) * PX
    advance = w * PX

    names = [".notdef"] + [f"uni{c:04X}" for c in range(first, first + len(glyph_rows))]
    cmap = {first + i: names[i + 1] for i in range(len(glyph_rows))}

    glyphs, lsb = {}, {}
    notdef_rows = [0x0000] * h
    box = [(1, 2, w - 1, 12)]
    for name, rows in zip(names, [notdef_rows] + glyph_rows):
        pen = TTGlyphPen(None)
        rs = box if name == ".notdef" else rects(rows, w, h)
        for (x0, y0, x1, y1) in rs:
            left, right = x0 * PX, x1 * PX
            top = asc - y0 * PX
            bottom = asc - y1 * PX
            pen.moveTo((left, bottom))
            pen.lineTo((left, top))
            pen.lineTo((right, top))
            pen.lineTo((right, bottom))
            pen.closePath()
        if name == ".notdef":
            for (x0, y0, x1, y1) in [(2, 3, w - 2, 11)]:
                left, right = x0 * PX, x1 * PX
                top, bottom = asc - y0 * PX, asc - y1 * PX
                pen.moveTo((left, bottom))
                pen.lineTo((right, bottom))
                pen.lineTo((right, top))
                pen.lineTo((left, top))
                pen.closePath()
        glyphs[name] = pen.glyph()
        lsb[name] = min((x0 for x0, _, _, _ in rs), default=0) * PX

    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (advance, lsb[n]) for n in names})
    fb.setupHorizontalHeader(ascent=asc, descent=-desc)
    fb.setupNameTable({"familyName": args.family, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=asc, sTypoDescender=-desc, sTypoLineGap=0,
                usWinAscent=asc, usWinDescent=desc, sxHeight=9 * PX, sCapHeight=12 * PX,
                panose=PANOSE_MONO, achVendID="ZUZU")
    fb.setupPost(isFixedPitch=1)
    fb.save(args.out)
    print(f"[ok] wrote {args.out}: {len(glyph_rows)} glyphs, advance {advance}, upem {UPEM}")


if __name__ == "__main__":
    main()
