#!/usr/bin/env python3
"""
Downscale a 1024×1024 final squircle render to the standard app-icon sizes
(16, 32, 64, 128, 256, 512). Lanczos filter — best quality for photographic
content with fine detail.

Usage:
    python3 make_iconset.py output/iter_v12_az5_squircle.png \\
        --out-dir output/iconset/

Writes:
    output/iconset/icon_16.png
    output/iconset/icon_32.png
    ... icon_512.png
    output/iconset/icon_1024.png   (copy of source)
"""
import argparse
import os
import shutil
from PIL import Image

DEFAULT_SIZES = [16, 32, 64, 128, 256, 512, 1024]


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("source", help="Final 1024×1024 squircle render.")
    p.add_argument("--out-dir", default="output/iconset", help="Output directory.")
    p.add_argument("--sizes", nargs="+", type=int, default=DEFAULT_SIZES,
                   help="Sizes to emit (default: 16 32 64 128 256 512 1024).")
    args = p.parse_args()

    src = Image.open(args.source).convert("RGBA")
    if src.width != src.height:
        raise ValueError(f"Source must be square, got {src.size}")

    os.makedirs(args.out_dir, exist_ok=True)

    for size in args.sizes:
        out_path = os.path.join(args.out_dir, f"icon_{size}.png")
        if size == src.width:
            shutil.copy(args.source, out_path)
        else:
            img = src.resize((size, size), Image.LANCZOS)
            img.save(out_path)
        print(f"  {size:>4}px  →  {out_path}")


if __name__ == "__main__":
    main()
