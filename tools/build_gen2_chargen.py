#!/usr/bin/env python3
"""Convert Uncle Bernie's nine-row glyph template to POM1's GEN2 ROM.

Usage: python3 tools/build_gen2_chargen.py roms/gen2_char.template roms/gen2_char.rom
The ROM contains 256 eight-row cells, 1 = lit, bit 0 = leftmost pixel.
The eighth column is retained in the file; GEN2 displays seven dots per cell.
"""
import argparse
from pathlib import Path
import re


def convert(text: str) -> bytes:
    lines = text.splitlines()
    glyphs = {}
    while lines:
        label, *lines = lines
        if not re.fullmatch(r"\$[0-9A-Fa-f]{2}", label):
            raise ValueError(f"invalid glyph label: {label!r}")
        code = int(label[1:], 16)
        if code in glyphs:
            raise ValueError(f"duplicate glyph ${code:02X}")
        rows, lines = lines[:9], lines[9:]
        if len(rows) != 9 or any(not re.fullmatch(r"[@-]{8}", row) for row in rows):
            raise ValueError(f"glyph ${code:02X} must have nine rows of eight @/- pixels")
        glyphs[code] = bytes(sum((pixel == '@') << x for x, pixel in enumerate(row))
                             for row in rows[:8])
    required = set(range(0x80)) | set(range(0xE0, 0x100))
    if set(glyphs) != required:
        raise ValueError("expected glyphs $00-$7F and $E0-$FF")
    # The normal uppercase/symbol cells are repeated in the upper bands.
    for code in range(0x80, 0xE0):
        glyphs[code] = glyphs[0x40 | (code & 0x3F)]
    return b"".join(glyphs[code] for code in range(256))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("template", type=Path)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    args.rom.write_bytes(convert(args.template.read_text(encoding="ascii")))


if __name__ == "__main__":
    main()
