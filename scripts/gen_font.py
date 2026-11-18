#!/usr/bin/env python3
"""Génère app/src/font8x16.h : police bitmap 8x16 (Latin-1) tirée de DejaVu Sans Mono Bold."""
import sys
from PIL import Image, ImageDraw, ImageFont

ttf = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
font = ImageFont.truetype(ttf, 13)
rows = []
for code in range(32, 256):
    img = Image.new("L", (8, 16), 0)
    if not (127 <= code < 160):
        ImageDraw.Draw(img).text((0, 0), chr(code), font=font, fill=255)
    px = img.load()
    rows.append([sum((1 << (7 - x)) for x in range(8) if px[x, y] > 96) for y in range(16)])

with open("app/src/font8x16.h", "w") as f:
    f.write("// Fichier généré par scripts/gen_font.py (DejaVu Sans Mono Bold, licence Bitstream Vera). Ne pas modifier.\n")
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write("static const uint8_t kFont8x16[224][16] = {\n")
    for r in rows:
        f.write("  {" + ",".join("0x%02x" % b for b in r) + "},\n")
    f.write("};\n")
