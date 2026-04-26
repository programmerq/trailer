#!/usr/bin/env python3
# TODO: Replace with real icon artwork before release
#
# Generates platform/linux/trailer-256.png: a minimal valid 256x256 PNG
# with a solid teal color (#0097A7). Run once to produce the PNG; the
# generated file is committed to the repo.
#
# Usage:
#   python3 platform/linux/generate-placeholder-icon.py

import struct
import zlib
import os

WIDTH = 256
HEIGHT = 256
# Teal color: #0097A7
R, G, B = 0x00, 0x97, 0xA7  # teal #0097A7


def make_png(width: int, height: int, r: int, g: int, b: int) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        length = struct.pack(">I", len(data))
        crc = struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        return length + tag + data + crc

    sig = b"\x89PNG\r\n\x1a\n"

    # IHDR field order: width, height, bit-depth, color-type(2=RGB), compression, filter, interlace
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr = chunk(b"IHDR", ihdr_data)

    # Each scanline: filter byte (0=None) followed by RGB triples
    row = bytes([0]) + bytes([r, g, b] * width)
    compressed = zlib.compress(row * height, 9)
    idat = chunk(b"IDAT", compressed)

    iend = chunk(b"IEND", b"")

    return sig + ihdr + idat + iend


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(script_dir, "trailer-256.png")
    png_data = make_png(WIDTH, HEIGHT, R, G, B)
    with open(out_path, "wb") as f:
        f.write(png_data)
    print(f"Written {len(png_data)} bytes to {out_path}")
