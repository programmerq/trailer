#!/usr/bin/env python3
"""
Composite a transparent Blender render onto a white rounded-square card with
proper inset margin (macOS app-icon convention).

Run on the host (uses local Pillow — not the Blender container):
    python3 postprocess.py output/icon_1024.png \\
        --out output/icon_1024_squircle.png \\
        --margin 0.06 --corner-radius 0.18

Defaults yield a 1024×1024 PNG with the icon contents on a white rounded-square
card occupying ~88% of the frame (6% transparent margin top/bottom/sides),
with corner radius ~18% of the card's side length.
"""
import argparse
import os
from PIL import Image, ImageDraw, ImageFilter


def make_rounded_square(size, card_size, corner_radius, fill=(255, 255, 255, 255)):
    """RGBA image of `size`×`size`, with a centered white rounded square of
    `card_size`×`card_size` and `corner_radius` corner radius."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    margin = (size - card_size) // 2
    x0, y0 = margin, margin
    x1, y1 = margin + card_size, margin + card_size
    draw.rounded_rectangle((x0, y0, x1, y1), radius=corner_radius, fill=fill)
    return img


def make_rounded_alpha_mask(size, card_size, corner_radius):
    """Single-channel mask (L mode): 255 inside the rounded square, 0 outside."""
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    margin = (size - card_size) // 2
    x0, y0 = margin, margin
    x1, y1 = margin + card_size, margin + card_size
    draw.rounded_rectangle((x0, y0, x1, y1), radius=corner_radius, fill=255)
    return mask


def composite(render_path, out_path, margin_frac=0.06, corner_radius_frac=0.18,
              shadow_offset=(0, 6), shadow_blur=14, shadow_alpha=40,
              clip_to_card=True):
    """
    Composite the transparent render onto a white rounded-square card.

    - margin_frac: fraction of the canvas reserved as transparent margin on
      all sides. 0.06 = 6% margin top/bottom/left/right.
    - corner_radius_frac: corner radius as fraction of the card's side length.
    - shadow_offset / shadow_blur / shadow_alpha: subtle drop shadow under the
      card so the icon grounds against light backgrounds in Finder/Dock.
    - clip_to_card: if True, the render's alpha is masked by the rounded
      square so nothing pokes outside the card.
    """
    src = Image.open(render_path).convert("RGBA")
    size = src.width
    if src.height != size:
        raise ValueError(f"Expected square render, got {src.size}")

    margin = int(round(size * margin_frac))
    card_size = size - 2 * margin
    corner_radius = int(round(card_size * corner_radius_frac))

    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # Drop shadow under the card.
    if shadow_alpha > 0:
        sh_mask = make_rounded_alpha_mask(size, card_size, corner_radius)
        shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        sh_layer = Image.new("RGBA", (size, size), (0, 0, 0, shadow_alpha))
        shadow.paste(sh_layer, (shadow_offset[0], shadow_offset[1]), sh_mask)
        shadow = shadow.filter(ImageFilter.GaussianBlur(shadow_blur))
        canvas = Image.alpha_composite(canvas, shadow)

    # White card.
    card = make_rounded_square(size, card_size, corner_radius)
    canvas = Image.alpha_composite(canvas, card)

    # Paste the render on top, optionally clipped to the card.
    if clip_to_card:
        clip_mask = make_rounded_alpha_mask(size, card_size, corner_radius)
        # Multiply src alpha by clip_mask.
        r, g, b, a = src.split()
        a = Image.eval(a, lambda v: v)  # ensure mode L
        # Multiply alpha and mask: pixel-wise minimum is a cheap mask intersect.
        a = Image.eval(a, lambda v: v)
        # Use ImageChops for proper multiply.
        from PIL import ImageChops
        clipped_a = ImageChops.multiply(a, clip_mask)
        src_clipped = Image.merge("RGBA", (r, g, b, clipped_a))
        canvas = Image.alpha_composite(canvas, src_clipped)
    else:
        canvas = Image.alpha_composite(canvas, src)

    canvas.save(out_path)
    print(f"[postprocess] {render_path} → {out_path}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", help="Transparent rendered PNG (square).")
    p.add_argument("--out", required=True, help="Output composited PNG.")
    p.add_argument("--margin", type=float, default=0.06,
                   help="Transparent margin around the white card, as fraction.")
    p.add_argument("--corner-radius", type=float, default=0.18,
                   help="Card corner radius, as fraction of card side length.")
    p.add_argument("--shadow-alpha", type=int, default=40,
                   help="Drop-shadow opacity (0-255). 0 to disable.")
    p.add_argument("--no-clip", action="store_true",
                   help="Don't clip the render to the rounded square.")
    args = p.parse_args()
    composite(
        render_path=args.input,
        out_path=args.out,
        margin_frac=args.margin,
        corner_radius_frac=args.corner_radius,
        shadow_alpha=args.shadow_alpha,
        clip_to_card=not args.no_clip,
    )


if __name__ == "__main__":
    main()
