# ML plumbing fails silently

Priority: P3. Tags: `ocr`.

- Done when: if the OCR dictionary fails to load, the OCR controls are disabled with a tooltip
  naming the cause. An unknown model id is handled without `Q_UNREACHABLE`.
- Src: `OcrEngine.cpp:353-355`; `ModelRegistry.cpp:43`.
