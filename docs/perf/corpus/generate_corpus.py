#!/usr/bin/env python3
"""Reproducibly generate the Trailer performance reference corpus.

Run from the repository root:

    python3 docs/perf/corpus/generate_corpus.py

Produces three small, synthetic files under docs/perf/corpus/:

  * form_1page.pdf   — a 1-page PDF carrying an interactive AcroForm
                       (one text field + one checkbox). Used by the
                       AcroForm open/render structural tests.
  * text_20page.pdf  — a 20-page text PDF. Used by the
                       render-before-full-read and paint-budget tests
                       that need a genuinely multi-page document.
  * photo.jpg        — a modest synthetic photograph (gradient + shapes).
                       Used by the image-open paint-budget test.

The PDFs are written with reportlab's ``invariant=1`` mode so the byte
output is stable across runs (fixed producer string, no wall-clock
timestamp), which keeps the checked-in corpus reproducible and its diffs
meaningful. The photo is generated from a fixed procedural recipe (no RNG
seed dependence) for the same reason.

Requires: reportlab, Pillow.  ``pip3 install reportlab pillow``
"""

import os

from reportlab.lib.pagesizes import letter
from reportlab.pdfgen import canvas
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))


def make_form_1page(path: str) -> None:
    """A single US-Letter page with an interactive AcroForm."""
    c = canvas.Canvas(path, pagesize=letter, invariant=1)
    width, height = letter
    c.setFont("Helvetica-Bold", 18)
    c.drawString(72, height - 90, "Trailer Perf Corpus — AcroForm sample")
    c.setFont("Helvetica", 11)
    c.drawString(72, height - 120, "Full name:")
    c.drawString(72, height - 160, "Subscribe:")

    form = c.acroForm
    # One interactive text field ...
    form.textfield(
        name="full_name",
        tooltip="Full name",
        x=150,
        y=height - 128,
        width=300,
        height=18,
        borderStyle="inset",
        forceBorder=True,
    )
    # ... and one checkbox, so the file exercises more than one widget type.
    form.checkbox(
        name="subscribe",
        tooltip="Subscribe",
        x=150,
        y=height - 165,
        size=16,
        borderStyle="inset",
        checked=False,
        forceBorder=True,
    )
    c.showPage()
    c.save()


def make_text_20page(path: str) -> None:
    """Twenty US-Letter pages of ordinary body text."""
    c = canvas.Canvas(path, pagesize=letter, invariant=1)
    width, height = letter
    for page in range(1, 21):
        c.setFont("Helvetica-Bold", 16)
        c.drawString(72, height - 80, f"Trailer Perf Corpus — Page {page} of 20")
        c.setFont("Helvetica", 11)
        y = height - 120
        for line in range(1, 41):
            c.drawString(
                72,
                y,
                f"Page {page}, line {line:02d}: the quick brown fox jumps "
                f"over the lazy dog. 0123456789.",
            )
            y -= 16
        c.showPage()
    c.save()


def make_photo(path: str) -> None:
    """A modest synthetic 'photo': a smooth gradient with a few shapes.

    Fully procedural (no RNG), so the bytes are reproducible. Kept small
    (640x480, JPEG quality 82) to stay well under the corpus budget.
    """
    w, h = 640, 480
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            r = (x * 255) // (w - 1)
            g = (y * 255) // (h - 1)
            b = ((x + y) * 255) // (w + h - 2)
            px[x, y] = (r, g, b)
    draw = ImageDraw.Draw(img)
    draw.ellipse([120, 90, 360, 300], fill=(240, 240, 210))
    draw.rectangle([380, 220, 560, 400], fill=(40, 60, 120))
    draw.line([0, h, w, 0], fill=(255, 255, 255), width=6)
    img.save(path, format="JPEG", quality=82, optimize=True)


def main() -> None:
    make_form_1page(os.path.join(HERE, "form_1page.pdf"))
    make_text_20page(os.path.join(HERE, "text_20page.pdf"))
    make_photo(os.path.join(HERE, "photo.jpg"))
    for name in ("form_1page.pdf", "text_20page.pdf", "photo.jpg"):
        p = os.path.join(HERE, name)
        print(f"{name}: {os.path.getsize(p)} bytes")


if __name__ == "__main__":
    main()
