# Image windows show a permanent "install language pack" link

Priority: P1. Tags: `chrome`, `ocr`.

- Done when: the link never shows once the OCR model is bundled ([The Latin OCR model isn't bundled, so scans aren't selectable on a fresh install](2026-10-15-bundle-ocr-model.md)); until then it shows
  at most once per document, in context, with a dismiss.
- Src: `MainWindow.cpp:722-733`, `:979-980`.
