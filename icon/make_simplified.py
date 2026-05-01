#!/usr/bin/env python3
"""
Simplified flat-color icon for very small sizes (≤64px).

The Blender hero render does not survive the squint test below ~128px —
the loupe collapses to a black blob and the canyon refraction becomes
mush. Apple's small-icon convention is to remake the artwork with a
stronger silhouette and flat color blocks; this script does that in
PIL (no Blender needed).

The simplified icon keeps the brand DNA:
  - white/dark card with the matching border (light- and dark-mode)
  - a horizontal sandstone-orange band where the film strip would be
  - a black rectangular silhouette of the loupe body
  - a small black circle on top for the lens housing (omitted at 16px)

Usage:
    python3 make_simplified.py --size 64 --out output/iconset/icon_64.png
    python3 make_simplified.py --size 64 --dark --out output/iconset_dark/icon_64.png
"""
import argparse
import os

from PIL import Image, ImageDraw


# Frame color + brand palette pulled from the Arches photo.
SANDSTONE = (181, 91, 50, 255)        # deep canyon red
SKY       = (108, 154, 195, 255)      # sky blue (used at 64+ as a thin slice)
BLACK     = (15, 15, 17, 255)
WHITE     = (255, 255, 255, 255)
OFF_WHITE = (241, 239, 234, 255)

# Dark-mode colors mirror postprocess.py.
DARK_CARD   = (28, 28, 30, 255)
DARK_BORDER = (58, 58, 60, 255)


def render_simplified(size, dark=False, margin_frac=0.10, corner_radius_frac=0.225,
                       border_frac=0.024):
    """Draw a flat-color simplified icon at `size`×`size`."""
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)

    margin = max(1, int(round(size * margin_frac)))
    card_size = size - 2 * margin
    corner_r = max(1, int(round(card_size * corner_radius_frac)))
    border_px = max(1, int(round(card_size * border_frac)))

    card_color = DARK_CARD if dark else OFF_WHITE
    border_color = DARK_BORDER if dark else WHITE

    # Outer border ring (filled with border color, body filled inside it).
    bx0, by0 = margin, margin
    bx1, by1 = margin + card_size, margin + card_size
    draw.rounded_rectangle((bx0, by0, bx1, by1), radius=corner_r, fill=border_color)

    inner_size = card_size - 2 * border_px
    inner_r = max(0, corner_r - border_px)
    ix0, iy0 = margin + border_px, margin + border_px
    ix1, iy1 = ix0 + inner_size, iy0 + inner_size
    draw.rounded_rectangle((ix0, iy0, ix1, iy1), radius=inner_r, fill=card_color)

    # Inside the card body, lay down (bottom→top):
    #   - sandstone band (the film strip's dominant color)
    #   - black loupe body silhouette (rounded rectangle)
    #   - small black circle (lens housing) — omitted below 32px
    cx = size // 2
    cy = size // 2

    # The "subject" footprint inside the card. Match the inner padding /
    # white border conventions of postprocess.py so light & dark icons
    # share visual proportions with the hero render.
    subject_inner_size = inner_size  # same area as the inner card body
    sub_x0 = ix0
    sub_x1 = ix1
    sub_y0 = iy0
    sub_y1 = iy1

    # Sandstone band — runs horizontally across the subject area, ~26% of
    # the subject height tall, vertically centered around 60% of subject
    # height (matches where the film strip ends up in the hero render after
    # the camera tilt and the body's vertical occupancy).
    band_h = max(2, int(round(subject_inner_size * 0.26)))
    band_cy = sub_y0 + int(subject_inner_size * 0.60)
    band_y0 = band_cy - band_h // 2
    band_y1 = band_y0 + band_h
    draw.rectangle((sub_x0, band_y0, sub_x1, band_y1), fill=SANDSTONE)

    # Loupe body silhouette: rectangular block sitting on the band, slightly
    # taller than the band so the housing sits above it.
    body_w = int(subject_inner_size * 0.42)
    body_h = int(subject_inner_size * 0.34)
    body_x0 = cx - body_w // 2
    body_x1 = body_x0 + body_w
    body_y1 = band_y1 - max(1, band_h // 4)  # bottom of body just above band's bottom
    body_y0 = body_y1 - body_h
    body_corner = max(1, body_h // 8)
    draw.rounded_rectangle((body_x0, body_y0, body_x1, body_y1),
                            radius=body_corner, fill=BLACK)

    # Lens housing: small black circle on top of the body. Skip at the very
    # smallest sizes — the body silhouette alone has to do the work.
    if size >= 32:
        housing_d = int(body_w * 0.55)
        housing_x0 = cx - housing_d // 2
        housing_y1 = body_y0 + max(1, body_h // 8)  # housing overlaps the body's top
        housing_y0 = housing_y1 - housing_d
        draw.ellipse((housing_x0, housing_y0, housing_x0 + housing_d, housing_y1),
                       fill=BLACK)

    return canvas


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--size", type=int, required=True)
    p.add_argument("--dark", action="store_true")
    p.add_argument("--out", required=True)
    args = p.parse_args()
    img = render_simplified(args.size, dark=args.dark)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    img.save(args.out)
    print(f"  simplified {args.size}px → {args.out}")


if __name__ == "__main__":
    main()
