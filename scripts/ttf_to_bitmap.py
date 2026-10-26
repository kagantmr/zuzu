#!/usr/bin/env python3
"""
ttf_to_bitmap_font.py — Rasterize a TTF into a fixed-size 1-bit bitmap font,
packed as a C header for use as a kernel/userspace glyph table (e.g. fbcons).

This is a one-time HOST-SIDE asset pipeline tool. It does not touch zuzu at
all — it just produces a .h file you #include or embed.

Usage:
    python3 ttf_to_bitmap_font.py --font MyFont.ttf --out font16x16.h

Output format:
    static const uint16_t font16x16[NUM_GLYPHS][16] = { ... };
    Row i of a glyph is one uint16_t. Bit 15 (MSB) = leftmost pixel,
    bit 0 (LSB) = rightmost of the 16 columns. font[ch - FIRST_CHAR] is
    the glyph for ASCII value 'ch'.

Also writes a PNG contact sheet (font16x16_preview.png) tiling every glyph
so you can eyeball the result before ever booting zuzu with it.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL_W = 12        # glyph cell width in pixels
CELL_H = 16        # glyph cell height in pixels (= em size for zuzufont12)
FIRST_CHAR = 32    # space
LAST_CHAR = 126    # '~'
THRESHOLD = 128    # grayscale cutoff for 1-bit conversion


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, align: str) -> tuple[Image.Image, int]:
    """Render one character into a CELL_W x CELL_H canvas, baseline-aligned.
    Returns the canvas and the glyph's ink width (to report clipping).
    align: 'left' keeps the font's own left bearing; 'center' centres the
    advance box; 'ink' centres the visible pixels."""
    wide = Image.new("L", (CELL_W * 3, CELL_H), 0)
    ImageDraw.Draw(wide).text((CELL_W, 0), ch, font=font, fill=255)
    bw = wide.point(lambda p: 255 if p >= THRESHOLD else 0)
    box = bw.getbbox()
    ink = (box[2] - box[0]) if box else 0
    if align == "ink" and box:
        x = (CELL_W - ink) // 2 - box[0]
    elif align == "center":
        x = (CELL_W - round(font.getlength(ch))) // 2 - CELL_W
    else:
        x = -CELL_W
    canvas = Image.new("L", (CELL_W, CELL_H), 0)
    canvas.paste(bw, (x, 0))
    return canvas, ink


def pack_rows(glyph_img: Image.Image) -> list[int]:
    """Threshold to 1-bit and pack each row into a uint16_t.
    Bit (CELL_W-1) = leftmost pixel (column 0), bit 0 = column CELL_W-1."""
    bw = glyph_img.point(lambda p: 255 if p >= THRESHOLD else 0)
    rows = []
    for y in range(CELL_H):
        word = 0
        for x in range(CELL_W):
            if bw.getpixel((x, y)) >= THRESHOLD:
                word |= 1 << (CELL_W - 1 - x)
        rows.append(word)
    return rows


def load_overrides(path: str) -> dict[int, list[int]]:
    """Parse hand-drawn glyphs: '@ <char>' then rows of CELL_W chars ('X'/'.')."""
    glyphs: dict[int, list[str]] = {}
    cur = None
    for n, raw in enumerate(open(path), 1):
        line = raw.rstrip("\n")
        if line.startswith("@ "):
            cur = ord(line[2])
            glyphs[cur] = []
        elif line.strip() == "" or line.startswith("#"):
            continue
        else:
            if cur is None:
                sys.exit(f"{path}:{n}: row before any '@ <char>'")
            if len(line) != CELL_W or set(line) - {"X", "."}:
                sys.exit(f"{path}:{n}: row must be {CELL_W} chars of X/.: {line!r}")
            glyphs[cur].append(line)
    rows: dict[int, list[int]] = {}
    for code, lines in glyphs.items():
        if len(lines) > CELL_H:
            sys.exit(f"{path}: glyph {chr(code)!r} has {len(lines)} rows, max {CELL_H}")
        lines += ["." * CELL_W] * (CELL_H - len(lines))
        rows[code] = [sum(1 << (CELL_W - 1 - x) for x, c in enumerate(l) if c == "X") for l in lines]
    return rows


def rows_to_image(rows: list[int]) -> Image.Image:
    img = Image.new("L", (CELL_W, CELL_H), 0)
    for y, word in enumerate(rows):
        for x in range(CELL_W):
            if word & (1 << (CELL_W - 1 - x)):
                img.putpixel((x, y), 255)
    return img


def build_preview(glyph_imgs: dict[int, Image.Image], cols: int = 16) -> Image.Image:
    """Tile all glyphs into one PNG for visual sanity-checking."""
    n = len(glyph_imgs)
    rows = (n + cols - 1) // cols
    sheet = Image.new("L", (cols * CELL_W, rows * CELL_H), 0)
    for i, ch in enumerate(sorted(glyph_imgs)):
        x = (i % cols) * CELL_W
        y = (i // cols) * CELL_H
        sheet.paste(glyph_imgs[ch], (x, y))
    # Scale up so it's actually visible when opened.
    return sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST)


def main():
    global CELL_W, CELL_H
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--font", required=True, help="Path to .ttf/.otf file")
    ap.add_argument("--out", default="font16x16.h", help="Output C header path")
    ap.add_argument("--array-name", default="font16x16", help="C array identifier")
    ap.add_argument("--preview", default="font16x16_preview.png")
    ap.add_argument("--cell-w", type=int, default=CELL_W)
    ap.add_argument("--cell-h", type=int, default=CELL_H)
    ap.add_argument("--size", type=int, default=0, help="render ppem (default: cell height)")
    ap.add_argument("--align", choices=("left", "center", "ink"), default="ink")
    ap.add_argument("--overrides", default=str(Path(__file__).with_name("zuzufont12_overrides.txt")),
                    help="hand-drawn glyph file ('' to disable)")
    args = ap.parse_args()
    CELL_W, CELL_H = args.cell_w, args.cell_h
    if CELL_W > 16:
        sys.exit("cell width must fit a uint16_t row")

    size = args.size or CELL_H
    font = ImageFont.truetype(args.font, size)
    print(f"[info] {CELL_W}x{CELL_H} cell, rendering at {size} ppem, metrics {font.getmetrics()}")

    glyph_imgs = {}
    glyph_rows = {}
    clipped = []
    for code in range(FIRST_CHAR, LAST_CHAR + 1):
        ch = chr(code)
        img, ink = render_glyph(font, ch, args.align)
        if ink > CELL_W:
            clipped.append(ch)
        glyph_imgs[code] = img
        glyph_rows[code] = pack_rows(img)
    if args.overrides and Path(args.overrides).exists():
        for code, rows in load_overrides(args.overrides).items():
            glyph_rows[code] = rows
            glyph_imgs[code] = rows_to_image(rows)
            if chr(code) in clipped:
                clipped.remove(chr(code))
        print(f"[info] applied overrides from {args.overrides}")
    if clipped:
        print(f"[warn] wider than the cell, clipped: {''.join(clipped)}")

    num_glyphs = LAST_CHAR - FIRST_CHAR + 1

    with open(args.out, "w") as f:
        f.write("// Auto-generated by ttf_to_bitmap.py — do not edit by hand.\n")
        f.write(f"// Source font: {args.font}\n")
        f.write(f"// {CELL_W}x{CELL_H} 1-bit glyphs, ASCII {FIRST_CHAR}..{LAST_CHAR}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define FONT_FIRST_CHAR {FIRST_CHAR}\n")
        f.write(f"#define FONT_LAST_CHAR  {LAST_CHAR}\n")
        f.write(f"#define FONT_GLYPH_W    {CELL_W}\n")
        f.write(f"#define FONT_GLYPH_H    {CELL_H}\n\n")
        f.write(f"static const uint16_t {args.array_name}[{num_glyphs}][{CELL_H}] = {{\n")
        for code in range(FIRST_CHAR, LAST_CHAR + 1):
            rows = glyph_rows[code]
            row_str = ", ".join(f"0x{r:04X}" for r in rows)
            ch_display = chr(code) if code != 32 else "space"
            f.write(f"    {{ {row_str} }}, // {code} '{ch_display}'\n")
        f.write("};\n")

    preview = build_preview(glyph_imgs)
    preview.save(args.preview)

    print(f"[ok] wrote {args.out} ({num_glyphs} glyphs)")
    print(f"[ok] wrote {args.preview} — inspect before using")


if __name__ == "__main__":
    main()
