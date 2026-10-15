# OCR on the reference machine is unmeasured

Priority: P2. Tags: `ocr`.

- Done when: with the weights present, OCR of one 300 dpi page uses at most one worker thread
  on battery, is cancellable within one page, makes no GUI-thread engine calls, and its time
  on the reference machine is in the closing PR's description.
- Src: no OCR run has been timed on old hardware.
