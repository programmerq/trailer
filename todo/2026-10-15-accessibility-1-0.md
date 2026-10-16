# Accessibility for 1.0

Priority: P3. Tags: `chrome`.

- Done when:
  - an automated sweep finds an accessible name on every actionable control;
  - every markup tool has a shortcut;
  - colour swatches show their state without relying on colour;
  - a signature can be typed;
  - a keyboard-only run completes PRODUCT.md's seven jobs at dpr 1 and 2;
  - contrast meets WCAG AA, focus is always visible, Reduce Motion is honoured, and text scales
    to 200 %.
- Src: `MarkupToolbar.cpp` has no `setShortcut`; `SignatureCaptureDialog.cpp:257`.
