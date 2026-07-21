---
name: trailer-v030-dogfood-synthesis
description: v0.3.0 real-Mac dogfood report + full history mine synthesized into 9 backlog items + a 6-theme UX research agenda — PR #53 MERGED to main 2026-07-14 (docs/research-synthesis @ a400b81)
metadata:
  type: project
---

The owner's v0.3.0 real-Mac dogfood (macOS Tahoe, 142MB text-layer PDF) was turned into an actionable research set: PR #53 (https://github.com/programmerq/trailer/pull/53), branch `docs/research-synthesis` @ `a400b81`, MERGED to main 2026-07-14. Docs-only (ci.yml paths-ignore docs/**, no CI). Two local review passes (completeness + skeptical line review) both PASS before push.

**9 backlog items** (`docs/backlog/2026-07-13-*`): startup-hang-large-pdf (P0, top billing — synchronous GUI-thread double-parse + eager all-pages annotation sweep in PdfDocument ctor; deterministic-proxy instrumentation plan, no CI wall-time gate), thumbnail-sidebar-sizing (P1), text-selection-and-recognize-notice (P1, clusters report #4+#5; selection reads OCR-only store that never ingests the native text layer, notice missing !hasTextLayer guard — refines ADR-0002 §3 on 4 points), search-current-page-seed (P2), toolbar-anchoring (P2), disabled-action-tooltip-visibility (TBD, history-mined), macos-dark-app-icon (P3 real-Mac), macos-screenrecording-services-clarity (P3 real-Mac), ship-upstream-license-files (TBD, LIC-CRIT-1). Research agenda: `docs/research/2026-07-13-ux-research-agenda.md`, 6 themes each terminating in an ADR/gate.

**Recurrence / process findings (priority signals):** the thumbnail complaint recurred across May HITL + PR #37, and **PR #37 merged a diagnosis-only doc with no code fix ever shipped** (same pattern in PR #26). History mine found ~79 deferred review nits across 48 PRs, and several substantive PRs (#40/#42/#47–#50/#52) merged with ZERO automated review. Extends [[trailer-followup-docket]].
