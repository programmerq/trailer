# TODO: Replace with real icon artwork before release
#
# Generates a solid-color 256x256 PNG using only stdlib (struct + zlib).
# No Pillow or other external dependencies required.
#
# Output: trailer-256.png in the same directory as this script.

import struct
import zlib
import os

WIDTH = 256
HEIGHT = 256
# Blue placeholder: #1565C0 (R=21, G=101, B=192)
R, G, B = 0x15, 0x65, 0xC0


def make_png(width: int, height: int, r: int, g: int, b: int) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        c = struct.pack(">I", len(data)) + tag + data
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return c + struct.pack(">I", crc)

    sig = b"\x89PNG\r\n\x1a\n"

    # bit_depth=8, color_type=2 (RGB truecolor), no compression/filter/interlace method variation
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr = chunk(b"IHDR", ihdr_data)

    # Filter byte 0 (None) prepended to each scanline, as required by the PNG spec
    row_pixel = bytes([r, g, b]) * width
    raw_data = b"".join(b"\x00" + row_pixel for _ in range(height))
    idat = chunk(b"IDAT", zlib.compress(raw_data, level=9))

    iend = chunk(b"IEND", b"")

    return sig + ihdr + idat + iend


if __name__ == "__main__":
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "trailer-256.png")
    png_bytes = make_png(WIDTH, HEIGHT, R, G, B)
    with open(out_path, "wb") as f:
        f.write(png_bytes)
    print(f"Written {len(png_bytes)} bytes to {out_path}")
