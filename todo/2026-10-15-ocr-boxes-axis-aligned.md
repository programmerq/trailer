# OCR boxes are axis-aligned, so selecting on a tilted scan is loose

Priority: P2. Tags: `ocr`, `text`.

- Done when: on a scan rotated 5°, a drag across two words of one line highlights those words
  and nothing from the lines above or below, at dpr 1 and 2.
- Src: the detector's quadrilaterals are reduced to axis-aligned rectangles
  (`OcrEngine.h:43-47`), so a tilted line's rectangle covers parts of its neighbours.
