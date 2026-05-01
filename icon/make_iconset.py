#!/usr/bin/env python3
"""
Build the standard app-icon size set from a 1024×1024 squircle render.

For each size:
  - ≤ `simplified_below`: redraw via make_simplified (flat-color silhouette
    that survives the squint test at 16/32/64 px).
  - Otherwise: downscale the hero render via Lanczos.

Usage:
    python3 make_iconset.py output/iter_X_squircle.png \\
        --out-dir output/iconset

    # Dark-mode iconset (consumes a --dark squircle, uses the dark-mode
    # simplified silhouette for the small sizes):
    python3 make_iconset.py output/iter_X_squircle_dark.png \\
        --out-dir output/iconset_dark --dark
"""
import argparse
import os
import shutil
from PIL import Image

import make_simplified

DEFAULT_SIZES = [16, 32, 64, 128, 256, 512, 1024]


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("source", help="Final 1024×1024 squircle render.")
    p.add_argument("--out-dir", default="output/iconset", help="Output directory.")
    p.add_argument("--sizes", nargs="+", type=int, default=DEFAULT_SIZES,
                   help="Sizes to emit (default: 16 32 64 128 256 512 1024).")
    p.add_argument("--simplified-below", type=int, default=128,
                   help="Sizes strictly less than this use the flat-color "
                        "simplified renderer; sizes ≥ this downscale the hero. "
                        "Default 128 (so 16/32/64 are simplified, 128+ are "
                        "downscaled hero).")
    p.add_argument("--dark", action="store_true",
                   help="Pass --dark through to make_simplified for the small "
                        "tier so the silhouette uses the dark-mode card.")
    args = p.parse_args()

    src = Image.open(args.source).convert("RGBA")
    if src.width != src.height:
        raise ValueError(f"Source must be square, got {src.size}")

    os.makedirs(args.out_dir, exist_ok=True)

    for size in args.sizes:
        out_path = os.path.join(args.out_dir, f"icon_{size}.png")
        if size < args.simplified_below:
            img = make_simplified.render_simplified(size, dark=args.dark)
            img.save(out_path)
            tag = "simplified-dark" if args.dark else "simplified"
        elif size == src.width:
            shutil.copy(args.source, out_path)
            tag = "hero (copy)"
        else:
            img = src.resize((size, size), Image.LANCZOS)
            img.save(out_path)
            tag = "hero (downscaled)"
        print(f"  {size:>4}px [{tag}] → {out_path}")


if __name__ == "__main__":
    main()
