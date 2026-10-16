# Moving a window between dpr 1 and dpr 2 screens doesn't re-render the image

Priority: P3. Tags: `open`.

- Done when: an image window dragged between a 2x and a 1x screen stays sharp ([Images at Fit are drawn at 1x and upscaled on HiDPI screens](2026-10-15-images-at-fit-blurry-hidpi.md) holds
  within one frame). At the pixel-exact stop it stays unresampled, with the pixmap stamped with
  the new screen's dpr. This has a test plus a real mixed-DPI check.
- Src: `ImageAdapter.h:318`.
