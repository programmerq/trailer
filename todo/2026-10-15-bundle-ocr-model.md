# The Latin OCR model isn't bundled, so scans aren't selectable on a fresh install

Priority: P2. Tags: `ocr`, `packaging`.

- Done when: on a fresh install with no network, opening a scanned PDF and dragging across a
  line selects and copies the right text with no download or consent prompt, on every OS's
  package; the bundled weights are SHA-256 checked, listed in `THIRD_PARTY_LICENSES.md`, and
  the model manager shows OCR as installed; background removal and Smart Lasso still ask
  before downloading.
- Src: owner decision 2026-10-15 (PRODUCT.md). Detector 2.4 MB, recognizer 9.0 MB, direction
  0.6 MB. Today every model downloads on first use (`src/ml/ModelDownloader.cpp`).
