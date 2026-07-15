# Vendored third-party license texts

This directory holds the verbatim upstream license and notice texts for the
software that ships **inside** a Trailer release artifact (binaries, DLLs, or
embedded resources). It exists so that the native packagers (`.deb`, `.rpm`,
`.msi`) can install the required attribution files alongside the application
and satisfy each dependency's redistribution terms.

The authoritative dependency list lives in
[`../../THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md). This directory
supplies the actual license *texts* that file only enumerates.

Trailer's own license is [`../../LICENSE`](../../LICENSE) (MIT) — it is not
duplicated here.

| File | Dependency | Version | License | Source (pinned) |
|------|------------|---------|---------|-----------------|
| `onnxruntime-LICENSE.txt` | ONNX Runtime | 1.25.0 | MIT | https://raw.githubusercontent.com/microsoft/onnxruntime/v1.25.0/LICENSE |
| `qpdf-LICENSE.txt` | qpdf | 12.3.2 | Apache-2.0 | https://raw.githubusercontent.com/qpdf/qpdf/v12.3.2/LICENSE.txt |
| `qpdf-NOTICE.md` | qpdf | 12.3.2 | Apache-2.0 (NOTICE) | https://raw.githubusercontent.com/qpdf/qpdf/v12.3.2/NOTICE.md |
| `libjpeg-turbo-LICENSE.md` | libjpeg-turbo (qpdf's JPEG dependency) | 3.0.3 | IJG / modified BSD (zlib) | https://raw.githubusercontent.com/libjpeg-turbo/libjpeg-turbo/3.0.3/LICENSE.md |
| `tomlplusplus-LICENSE.txt` | toml++ | 3.4.0 | MIT | https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/LICENSE |
| `paddleocr-LICENSE.txt` | PaddleOCR (`ppocr_en_dict.txt`, embedded) | v2.7.0 | Apache-2.0 | https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/v2.7.0/LICENSE |
| `qt-LGPL-3.0.txt` | Qt 6 (dynamically linked) | 6.11 | LGPL-3.0 | https://www.gnu.org/licenses/lgpl-3.0.txt |
| `qt-GPL-3.0.txt` | Qt 6 (referenced by LGPL-3.0) | 6.11 | GPL-3.0 | https://www.gnu.org/licenses/gpl-3.0.txt |

## Notes

- **Qt** is dynamically linked and distributed under LGPL-3.0. The LGPL-3.0
  text incorporates the GPL-3.0 by reference, so both are vendored. The
  canonical texts are the FSF-published versions (identical to the copies Qt
  ships as `LICENSE.LGPLv3` / `LICENSE.GPLv3`).
- **libjpeg-turbo** is pulled in as a static dependency of qpdf (see
  `scripts/build-macos.sh` / `build-windows.sh`, which pin `LIBJPEG_VERSION=3.0.3`
  and `QPDF_VERSION=12.3.2`). Its `LICENSE.md` covers the IJG license plus the
  modified BSD (zlib-style) terms that apply to the TurboJPEG portions.
- **PaddleOCR** does **not** publish a standalone `NOTICE` file in its
  repository (verified absent at `v2.7.0`, and on `main`, as of 2026-07). Its
  Apache-2.0 attribution requirement is satisfied by the copyright header at
  the top of the vendored `paddleocr-LICENSE.txt`
  (`Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserved`). Only the
  English dictionary `resources/ppocr_en_dict.txt` is embedded in the binary;
  the ONNX model weights are downloaded at runtime and are not redistributed.

## Refresh procedure

When a pinned dependency version changes, update `THIRD_PARTY_LICENSES.md` in
the same commit, then re-fetch the corresponding text here from the URL in the
table above (bumping the tag). Verify each file is non-empty and matches the
expected license before committing.
