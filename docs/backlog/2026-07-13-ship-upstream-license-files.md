---
id: 2026-07-13-ship-upstream-license-files
title: Packages must ship upstream LICENSE/NOTICE files for bundled deps (ONNX, qpdf, PaddleOCR, Qt) — legal exposure before first release
priority: P1
status: open
source: recurring nit (history mine, 2026-07-13)
created: 2026-07-13
---

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

Tracked in-tree at `TODO-packaging.md:22` and `docs/audit-2026-05-19.md:487`
(LIC-CRIT-1). It is adjacent to the broader "CMake has no `install()` rules"
packaging thread raised repeatedly and never reconciled on PRs #4 and #5 (e.g.
`%license`/`%doc` bare paths unreachable without `%setup`; missing
`-DCMAKE_INSTALL_PREFIX=/usr`), and to the packaging-wiring backlog item.

This item is scoped narrowly to the **license/notice shipping** obligation (the
legal gap), not the whole install-rules refactor. It is filed because a legal
obligation should be tracked to a checkable threshold rather than left only as an
audit finding.

Cross-links: `2026-07-13-wire-msi-deb-rpm-packagers` (the packagers this rides
with); `docs/audit-2026-05-19.md:487` (LIC-CRIT-1); `TODO-packaging.md:22`.

## Provenance

Recurring nit from the PR/doc history mine, 2026-07-13. Not owner-ranked at
source; priority P1 assigned here on the legal-exposure / pre-first-release basis
— re-triage when picked up.
