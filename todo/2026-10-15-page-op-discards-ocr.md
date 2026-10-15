# Any page operation discards OCR for every page

Priority: P2. Tags: `ocr`.

- Done when: rotate page 1: page 2 keeps its OCR, and page 1's is rotated or recomputed once.
- Src: `PdfAdapter.cpp:2140-2146` clears all OCR.
