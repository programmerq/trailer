# OCR'd text selects whole blocks, not words

Priority: P1. Tags: `ocr`, `text`.

- Done when: dragging across two words of an OCR'd line selects and copies exactly those two
  words, as native text does at the same spot. Checked in Continuous, Single and Two Pages at
  dpr 1 and 2.
- Src: `SelectableTextLayer.cpp` does no word splitting.
