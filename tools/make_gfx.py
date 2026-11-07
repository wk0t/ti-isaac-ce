# -*- coding: utf-8 -*-
"""Genere les PNG de sprites + convimg.yaml + palette.h depuis sprites_art.py.

Usage: python -X utf8 make_gfx.py
Sorties:
  src/gfx/<sprite>.png        (RGB, transparent = magenta 255,0,255)
  src/gfx/convimg.yaml        (palette fixe complete, convert "sprites")
  src/palette.h               (#define COL_<NOM> <index>)
  icon.png                    (16x16 pour le makefile CEdev)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from sprites_art import PALETTE, ART, DERIVED, ROTATED  # noqa: E402
from PIL import Image                                      # noqa: E402

CHAR2RGB = {}
CHAR2IDX = {}
for idx, ch, name, rgb in PALETTE:
    if ch in ('.',):
        CHAR2RGB[ch] = (255, 0, 255)
    else:
        CHAR2RGB[ch] = rgb
    CHAR2IDX[ch] = idx

errors = []


def grid_from(name, rows):
    w, h = None, len(rows)
    for i, row in enumerate(rows):
        if w is None:
            w = len(row)
        if len(row) != w:
            errors.append("%s: ligne %d largeur %d != %d" % (name, i, len(row), w))
        for ch in row:
            if ch not in CHAR2RGB:
                errors.append("%s: ligne %d caractere inconnu '%s'" % (name, i, ch))
    return rows, w, h


def recolor(rows, table):
    return ["".join(table.get(ch, ch) for ch in row) for row in rows]


def rotate_ccw(rows):
    # 90 deg anti-horaire : colonne droite devient premiere ligne
    w = len(rows[0])
    out = []
    for c in range(w - 1, -1, -1):
        out.append("".join(rows[r][c] for r in range(len(rows))))
    return out


def emit_png(path, rows):
    w = len(rows[0])
    h = len(rows)
    img = Image.new("RGB", (w, h), (255, 0, 255))
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            px[x, y] = CHAR2RGB[ch]
    img.save(path)


ALL_SPRITES = {}


def add_grid(name, rows):
    fixed, w, h = grid_from(name, rows)
    ALL_SPRITES[name] = fixed


for name, rows in ART.items():
    add_grid(name, rows)

for name, (src, table) in DERIVED.items():
    add_grid(name, recolor(ART[src], table))

for name, src in ROTATED.items():
    add_grid(name, rotate_ccw(ART[src]))

if errors:
    for e in errors:
        print("ERREUR ART:", e)
    sys.exit(1)

# --- emission PNG -------------------------------------------------------
gfx_dir = os.path.join(ROOT, "src", "gfx")
os.makedirs(gfx_dir, exist_ok=True)

names = sorted(ALL_SPRITES.keys())
for name in names:
    emit_png(os.path.join(gfx_dir, name + ".png"), ALL_SPRITES[name])

# --- convimg.yaml --------------------------------------------------------
y = []
y.append("palettes:")
y.append("  - name: global_palette")
y.append("    fixed-entries:")
for idx, ch, pname, rgb in PALETTE:
    y.append("      - color: {index: %d, r: %d, g: %d, b: %d}  # %s"
             % (idx, rgb[0], rgb[1], rgb[2], pname))
y.append("    images: automatic")
y.append("")
y.append("converts:")
y.append("  - name: sprites")
y.append("    palette: global_palette")
y.append("    transparent-color-index: 0")
y.append("    images:")
for name in names:
    y.append("      - %s.png" % name)
y.append("")
y.append("outputs:")
y.append("  - type: c")
y.append("    include-file: gfx.h")
y.append("    palettes:")
y.append("      - global_palette")
y.append("    converts:")
y.append("      - sprites")
with open(os.path.join(gfx_dir, "convimg.yaml"), "w", newline="\n") as fh:
    fh.write("\n".join(y) + "\n")

# --- palette.h -----------------------------------------------------------
h = []
h.append("/* Genere par tools/make_gfx.py — indice = index convimg global_palette */")
h.append("#ifndef PALETTE_H")
h.append("#define PALETTE_H")
for idx, ch, pname, rgb in PALETTE:
    h.append("#define COL_%-11s %d" % (pname, idx))
h.append("")
h.append("#endif /* PALETTE_H */")
with open(os.path.join(ROOT, "src", "palette.h"), "w", newline="\n") as fh:
    fh.write("\n".join(h) + "\n")

# --- icon 16x16 (tete Isaac) --------------------------------------------
ICON = [
    "................",
    "....ssssssss....",
    "..ssssssssssss..",
    "..ssssssssssss..",
    "..ss.KK..KK.ss..",
    "..ss.KK..KK.ss..",
    "..ss.C....C.ss..",
    "..ssssssssssss..",
    "..ssssssssssss..",
    "...ssssssssss...",
    "....SssssssS....",
    "......ssss......",
    ".....ssssss.....",
    "................",
    "................",
    "................",
]
grid_from("icon", ICON)
if errors:
    for e in errors:
        print("ERREUR ICON:", e)
    sys.exit(1)
emit_png(os.path.join(ROOT, "icon.png"), ICON)

print("OK %d sprites :" % len(ALL_SPRITES), ", ".join(names))
print("palette: %d couleurs ; convimg.yaml + palette.h + icon.png ecrits" % len(PALETTE))
