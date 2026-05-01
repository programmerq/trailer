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
    """RGBA image of `size`×`size`, with a centered rounded square of
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


def make_subject_shadow(subject_layer, blur=18, offset=(0, 8), darkness=80):
    """Take the alpha of a subject layer and turn it into a soft dark drop
    shadow at the same canvas size."""
    a = subject_layer.split()[-1]
    shadow = Image.new("RGBA", subject_layer.size, (0, 0, 0, 0))
    sh_color = Image.new("RGBA", subject_layer.size, (0, 0, 0, darkness))
    shadow.paste(sh_color, offset, a)
    shadow = shadow.filter(ImageFilter.GaussianBlur(blur))
    return shadow


def composite(render_path, out_path, margin_frac=0.10, corner_radius_frac=0.225,
              inner_padding_frac=0.10,
              card_color=(255, 255, 255, 255),
              canvas_bg_color=(241, 239, 234, 255),
              shadow_offset=(0, 6), shadow_blur=14, shadow_alpha=40,
              subject_shadow_offset=(0, 8), subject_shadow_blur=18,
              subject_shadow_alpha=70):
    """
    Composite the transparent render onto a slightly off-white rounded-square
    card with two drop shadows (one under the card, one under the rendered
    subject onto the card).

    Three nested concentric rounded squares:
      1. Canvas: 1024×1024 (or matching).
      2. Outer card: `margin_frac` of canvas on each side is transparent.
         At default 10% the card is ~80% of canvas (matches Apple's macOS
         Big Sur+ template — 824/1024).
      3. Inner safe-area: card minus `inner_padding_frac` on each side.
         The rendered subject is scaled to fit this area AND clipped to a
         rounded-square mask of the same proportions as the card. Clipping
         to an inner rounded shape (rather than the outer card) gives a
         CONSTANT border distance between any rendered element and the
         visible card edge — without it, a diagonal element (like the film
         strip) gets visibly closer to the corners than the straight edges.

    - corner_radius_frac: corner radius as fraction of the card's side length.
    - card_color: RGBA color of the card. Default is a barely-warm off-white
      (slightly different from the rendered white world environment so the
      card has visible edges against any white-ish reflections in the
      subject).
    - shadow_*: drop shadow under the card.
    - subject_shadow_*: drop shadow under the subject, cast onto the card.
    """
    from PIL import ImageChops

    src = Image.open(render_path).convert("RGBA")
    size = src.width
    if src.height != size:
        raise ValueError(f"Expected square render, got {src.size}")

    margin = int(round(size * margin_frac))
    card_size = size - 2 * margin
    corner_radius = int(round(card_size * corner_radius_frac))

    # Inner safe-area: a rounded square that's everywhere `inner_pad_px`
    # inside the card. Crucially, the corner CENTERS coincide with the
    # outer card's corner centers — only the radius shrinks. Scaling the
    # radius proportionally instead leaves the diagonal ends of any
    # element (like the rotated film strip) noticeably closer to the
    # card's corners than the straight edges are to the straight edges.
    inner_pad_px = int(round(card_size * inner_padding_frac))
    inner_size = card_size - 2 * inner_pad_px
    inner_corner_radius = max(0, corner_radius - inner_pad_px)

    # Canvas background: off-white by default so the white card edge has
    # contrast against the surrounding margin. Set canvas_bg_color=(0,0,0,0)
    # for a fully transparent canvas instead.
    canvas = Image.new("RGBA", (size, size), canvas_bg_color)

    # Drop shadow under the card.
    if shadow_alpha > 0:
        sh_mask = make_rounded_alpha_mask(size, card_size, corner_radius)
        shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        sh_layer = Image.new("RGBA", (size, size), (0, 0, 0, shadow_alpha))
        shadow.paste(sh_layer, (shadow_offset[0], shadow_offset[1]), sh_mask)
        shadow = shadow.filter(ImageFilter.GaussianBlur(shadow_blur))
        canvas = Image.alpha_composite(canvas, shadow)

    # Off-white card.
    card = make_rounded_square(size, card_size, corner_radius, fill=card_color)
    canvas = Image.alpha_composite(canvas, card)

    # Scale rendered subject to fit the inner safe-area.
    if inner_size != size:
        subject = src.resize((inner_size, inner_size), Image.LANCZOS)
    else:
        subject = src

    subject_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    paste_xy = (margin + inner_pad_px, margin + inner_pad_px)
    subject_layer.paste(subject, paste_xy, subject)

    # Clip the subject layer to the INNER rounded square so its perimeter
    # follows a constant inset from the card edge.
    inner_mask = make_rounded_alpha_mask(size, inner_size, inner_corner_radius)
    r, g, b, a = subject_layer.split()
    clipped_a = ImageChops.multiply(a, inner_mask)
    subject_layer = Image.merge("RGBA", (r, g, b, clipped_a))

    # Subject drop shadow on the card. Generated from the inner-clipped
    # subject so the shadow follows the visible silhouette, not the original
    # render's full alpha.
    if subject_shadow_alpha > 0:
        subject_shadow = make_subject_shadow(
            subject_layer,
            blur=subject_shadow_blur,
            offset=subject_shadow_offset,
            darkness=subject_shadow_alpha,
        )
        # Confine the shadow to the card area (don't bleed past the squircle).
        outer_mask = make_rounded_alpha_mask(size, card_size, corner_radius)
        sr, sg, sb, sa = subject_shadow.split()
        sa = ImageChops.multiply(sa, outer_mask)
        subject_shadow = Image.merge("RGBA", (sr, sg, sb, sa))
        canvas = Image.alpha_composite(canvas, subject_shadow)

    canvas = Image.alpha_composite(canvas, subject_layer)

    canvas.save(out_path)
    print(f"[postprocess] {render_path} → {out_path}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", help="Transparent rendered PNG (square).")
    p.add_argument("--out", required=True, help="Output composited PNG.")
    p.add_argument("--margin", type=float, default=0.10,
                   help="Transparent margin around the white card, as fraction "
                        "of canvas. Default 0.10 leaves the card at 80%% of the "
                        "canvas — matches Apple's macOS Big Sur+ template.")
    p.add_argument("--corner-radius", type=float, default=0.225,
                   help="Card corner radius, as fraction of card side length.")
    p.add_argument("--inner-padding", type=float, default=0.10,
                   help="White-border padding inside the card. The rendered "
                        "subject is scaled to fit (card - 2*inner_padding), "
                        "so the white card edge has clean breathing room. "
                        "0 disables.")
    p.add_argument("--shadow-alpha", type=int, default=40,
                   help="Card drop-shadow opacity (0-255). 0 to disable.")
    p.add_argument("--subject-shadow-alpha", type=int, default=70,
                   help="Subject drop-shadow opacity (0-255). 0 to disable.")
    p.add_argument("--transparent-bg", action="store_true",
                   help="Use a fully transparent canvas instead of the default "
                        "off-white. Use this when the output is going into the "
                        "macOS .icns / Windows .ico — those want transparency "
                        "around the rounded square.")
    args = p.parse_args()
    canvas_bg = (0, 0, 0, 0) if args.transparent_bg else (241, 239, 234, 255)
    composite(
        render_path=args.input,
        out_path=args.out,
        margin_frac=args.margin,
        corner_radius_frac=args.corner_radius,
        inner_padding_frac=args.inner_padding,
        shadow_alpha=args.shadow_alpha,
        subject_shadow_alpha=args.subject_shadow_alpha,
        canvas_bg_color=canvas_bg,
    )


if __name__ == "__main__":
    main()
