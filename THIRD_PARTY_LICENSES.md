# Third-Party Notices

Trailer is distributed under the [MIT License](LICENSE). It links against
and/or redistributes the following third-party components, each under its
own license. This file enumerates them so downstream packagers can satisfy
attribution requirements.

The list is split into two sections:

1. **Software linked or bundled with the Trailer binary** — these ship
   alongside the app and their licenses apply to every release artifact.
2. **Model weights downloaded on first use** — Trailer does not bundle
   these; they are fetched from their upstream hosts the first time a
   feature needs them, then cached on disk. Their licenses apply to the
   downloaded files, not to Trailer itself.

If you add a new dependency, cache a new model, or bump a pinned version,
update the corresponding row here in the same commit.

---

## 1. Linked / bundled software

### Qt 6

- **Used for:** application framework (Widgets, GUI, Network, PDF,
  PdfWidgets, PrintSupport, Test).
- **License:** GNU Lesser General Public License, version 3.0 (LGPL-3.0);
  parts under GPL-3.0 with exception; commercial licenses also available
  from The Qt Company.
- **Upstream:** <https://www.qt.io/>
- **Notice:** Trailer dynamically links Qt. Redistributors must comply
  with LGPL-3.0 relinking obligations — typically satisfied by shipping
  the unmodified Qt shared libraries and a copy of the LGPL-3.0 text.

### ONNX Runtime 1.25.0

- **Used for:** executing exported ONNX models (U²-Net, MobileSAM,
  PP-OCRv3). Fetched as a prebuilt archive from the Microsoft GitHub
  release page at configure time by `cmake/OnnxRuntime.cmake`.
- **License:** MIT License.
- **Copyright:** © Microsoft Corporation.
- **Upstream:** <https://github.com/microsoft/onnxruntime>
- **Notice:** the runtime shared library (`onnxruntime.dll` /
  `libonnxruntime.so*` / `libonnxruntime.dylib`) is redistributed next to
  the Trailer executable. Include the upstream `LICENSE` file in any
  binary distribution.

### qpdf

- **Used for:** PDF editing primitives (merge, split, crop, page
  reordering, linearization, stream compression, password handling,
  AcroForm I/O, signatures, redaction).
- **License:** Apache License 2.0 (qpdf ≥ 11); earlier versions were
  Artistic-2.0 — verify which your distribution is using.
- **Upstream:** <https://github.com/qpdf/qpdf>
- **Notice:** Trailer links against the system or vcpkg-provided
  `libqpdf`. If you statically link qpdf into Trailer, include qpdf's
  `NOTICE` and `LICENSE` files in your distribution.

### toml++ 3.4.0

- **Used for:** parsing and writing `settings.toml`.
- **License:** MIT License.
- **Copyright:** © Mark Gillard.
- **Upstream:** <https://github.com/marzer/tomlplusplus>
- **Notice:** fetched via `FetchContent` and compiled into
  `trailer_core`.

### PaddleOCR English dictionary (`resources/ppocr_en_dict.txt`)

- **Used for:** CTC-decode index → character mapping for the PP-OCRv3
  Latin recognizer. Embedded into the Trailer binary via
  `resources/trailer.qrc` as `:/ml/ppocr_en_dict.txt`.
- **License:** Apache License 2.0.
- **Upstream:** <https://github.com/PaddlePaddle/PaddleOCR> — see
  `ppocr/utils/en_dict.txt` in the upstream repository.
- **Notice:** the file is verbatim from PaddleOCR. The Apache-2.0 license
  text must accompany any distribution that includes the Trailer binary.

### Application icons and other assets under `resources/`

- **License:** MIT (same as Trailer). Contributors grant copyright to the
  project under the `Trailer contributors` heading.

---

## 2. Model weights downloaded on first use

Trailer's ML features (Background Removal, Instant Alpha, Smart Lasso,
Recognize Text) fetch ONNX weights from the URLs pinned in
`src/ml/ModelRegistry.cpp` the first time the user invokes them. The
downloaded files land in a platform-specific cache under
`AppPaths::modelsDir()` and are not re-redistributed by Trailer. Their
licenses govern the downloaded files; the table below is provided so
packagers who choose to **pre-seed** the cache (e.g. air-gapped installs)
know what they are shipping.

Verified SHA-256 hashes in the Trailer manifest match the HuggingFace
LFS `oid` for each file — a bit-for-bit integrity check happens on every
download.

| Model | File | License | Upstream | Used for |
|-------|------|---------|----------|----------|
| U²-Net Portable | `u2netp.onnx` (≈ 4.6 MB) | Apache 2.0 | <https://github.com/xuebinqin/U-2-Net> (weights re-hosted at <https://github.com/danielgatis/rembg>) | Background Removal |
| MobileSAM image encoder | `mobile_sam_encoder.onnx` (≈ 28.2 MB) | Apache-2.0 weights, MIT ONNX export | <https://github.com/ChaoningZhang/MobileSAM> (export: <https://huggingface.co/Acly/MobileSAM>) | Instant Alpha, Smart Lasso |
| MobileSAM prompt decoder | `mobile_sam_decoder.onnx` (≈ 16.5 MB) | Apache-2.0 weights, MIT ONNX export | same | Instant Alpha, Smart Lasso |
| PP-OCRv3 text detector (English) | `pp_ocr_det.onnx` (≈ 2.4 MB) | Apache 2.0 | <https://github.com/PaddlePaddle/PaddleOCR> (export: <https://huggingface.co/SWHL/RapidOCR>) | Recognize Text |
| PP-OCRv3 Latin recognizer | `pp_ocr_rec_en.onnx` (≈ 9.0 MB) | Apache 2.0 | same | Recognize Text |
| PP-OCR direction classifier | `pp_ocr_cls.onnx` (≈ 0.6 MB) | Apache 2.0 | same | *pinned but not yet loaded — reserved for a future rotation pre-pass* |
| PP-OCRv4 CJK recognizer | `pp_ocr_rec_cjk.onnx` (≈ 10.9 MB) | Apache 2.0 | same | *pinned but not yet loaded — reserved for a future CJK language pack* |
| BiRefNet Lite | `birefnet_lite.onnx` (size TBD) | MIT | <https://github.com/ZhengPeng7/BiRefNet> | *registered in `ModelRegistry` but download URL + SHA-256 not yet pinned — reserved for a future high-precision background-removal alternative* |

### Notices for Apache-2.0 weights

Apache 2.0 requires that any distribution that includes the downloaded
weights also includes a copy of the license and the upstream `NOTICE`
file (where present). If you pre-seed Trailer's model cache with any of
the rows above, include:

- `LICENSE-APACHE-2.0.txt` (one copy is enough for all Apache-2.0 items).
- The upstream `NOTICE` file for each project whose weights you are
  redistributing. For U²-Net, MobileSAM, and PaddleOCR those live in the
  respective upstream repositories.

---

## Updating this file

The `add()` call for every entry in `ModelRegistry::populateBuiltin()`
carries a license string and upstream URL. Any change to that list —
new model, new version, new URL — requires a corresponding update here
in the same commit. The unit test `TestOcrEngine` and the download-path
tests in `TestBackgroundRemover` / `TestSamSession` cover manifest
integrity; there is no separate license-file lint (yet).
