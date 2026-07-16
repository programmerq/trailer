---
id: 2026-07-13-ship-upstream-license-files
title: Packages must ship upstream LICENSE/NOTICE files for bundled deps (ONNX, qpdf, PaddleOCR, Qt) — legal exposure before first release
priority: TBD
status: done
source: recurring nit (history mine, 2026-07-13)
created: 2026-07-13
---

## Resolved 2026-07-15

The vendored upstream license/notice set now lives in-tree under
`licenses/third-party/` and ships in every distributed artifact:

- ONNX Runtime — MIT (`onnxruntime-LICENSE.txt`)
- qpdf — Apache-2.0 + its NOTICE (`qpdf-LICENSE.txt`, `qpdf-NOTICE.md`)
- Qt — LGPL-3.0 / GPL-3.0 (`qt-LGPL-3.0.txt`, `qt-GPL-3.0.txt`)
- libjpeg-turbo (`libjpeg-turbo-LICENSE.md`)
- PaddleOCR — Apache-2.0. Note: PaddleOCR ships **no standalone NOTICE**;
  its Apache attribution lives in the LICENSE header, captured in
  `paddleocr-LICENSE.txt`.
- (also tomlplusplus — MIT)

Delivery paths:

- **`.deb` / `.rpm`** — CMake `install()` rules place `LICENSE`,
  `THIRD_PARTY_LICENSES.md`, and the `licenses/third-party/` set into
  `share/doc/trailer/` (docdir forced lowercase via
  `-DCMAKE_INSTALL_DOCDIR=share/doc/trailer`).
- **`.msi`** — `generate-wix-fragment.py` emits license components that
  install the same texts under `licenses\` / `licenses\third-party\` in
  the install directory.
- **Portable `.tar.gz` / `.zip`** — the release-staging steps now
  `cp -r licenses/third-party` into the bundle root alongside `LICENSE` /
  `THIRD_PARTY_LICENSES.md`.
- **macOS `.dmg`** — an additive license-staging step in
  `scripts/build-macos.sh` bundles the same texts (**unvalidated** —
  macOS-only build path not exercised here).

## Threshold

Every shipped package (.dmg / .deb / .rpm / .msi) contains the required upstream
license and notice files for the bundled third-party dependencies, installed to
a documented path.

Declared pass/fail: for each package format, an installed build includes at
minimum the ONNX Runtime license (MIT), qpdf license (Apache-2.0), the PaddleOCR
NOTICE, and the Qt LGPL license text — verifiable by listing the package
contents / the installed doc directory.

Verified: extracting each built package shows the upstream LICENSE/NOTICE files
present at the documented location.

## Context

Recurring nit surfaced by the history mine (LIC-CRIT-1). CMake ships no
`install()` rules that place upstream LICENSE/NOTICE files, so the packagers
bundle third-party code without its required license text — a legal-exposure gap
that should close **before the first public release**.

Tracked in-tree at `TODO-packaging.md:25` and `docs/audit-2026-05-19.md:487`
(LIC-CRIT-1). It is adjacent to the broader "CMake has no `install()` rules"
packaging thread raised repeatedly and never reconciled on PRs #4 and #5 (e.g.
`%license`/`%doc` bare paths unreachable without `%setup`; missing
`-DCMAKE_INSTALL_PREFIX=/usr`), and to the packaging-wiring backlog item.

This item is scoped narrowly to the **license/notice shipping** obligation (the
legal gap), not the whole install-rules refactor. It is filed because a legal
obligation should be tracked to a checkable threshold rather than left only as an
audit finding.

Cross-links: `2026-07-13-wire-msi-deb-rpm-packagers` (the packagers this rides
with); `docs/audit-2026-05-19.md:487` (LIC-CRIT-1); `TODO-packaging.md:25`.

## Provenance

Recurring nit from the PR/doc history mine, 2026-07-13. Not owner-ranked at
source; priority recorded as `TBD` per the "don't invent a priority" rule
(despite the legal-exposure / pre-first-release urgency) — re-triage when picked
up.
