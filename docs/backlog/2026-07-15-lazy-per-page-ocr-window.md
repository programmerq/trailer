---
id: 2026-07-15-lazy-per-page-ocr-window
title: Lazy per-page OCR window for large/multi-page documents (visible ±2, on-demand on jump, OCR-as-scanned for search/replace)
priority: P2
status: open
source: ADR 0012 (OCR pipeline for images) — G12.3, deferred past the images PR
created: 2026-07-15
---

## Threshold

Per **ADR 0012 §G12.3**. For a document above the small-image threshold (or any
multi-page document), background OCR is confined to the visible page(s) **± 2**
(N = 2, a 5-page window), recenters on demand when the user jumps, OCRs pages as
a search/replace scan reaches them, and stays inside the existing
`[ml.scheduler]` CPU/battery gates. Declared pass/fail (OCR batch/handle spies +
offscreen `QWidget::grab()` per AGENTS.md G2):

1. **Window.** After settling on page *k*, pages `[k-2, k+2]` are OCR-submitted
   and **no** page outside that window is.
2. **Jump.** Jumping to page *j* recenters OCR on `[j-2, j+2]`; pages outside it
   are not submitted, and prior speculative pages are cancelled (on-demand, not
   greedy).
3. **Search/replace.** A scan submits pages **only as it reaches them**, never
   the whole document at once, and search/replace finds text on not-yet-visited
   pages.
4. **CPU discipline.** With `run_on_battery=false` on battery, only the
   `VisiblePage` submission runs; the ±2 neighbours and scan-ahead
   (`Prefetch`/`Idle`) are suppressed. With `recognize_text_in_background=false`,
   there are **zero** background OCR submissions (explicit Recognize Text
   unaffected).

## Context

ADR 0012 accepts the owner's tiered OCR policy: small images auto-OCR eagerly,
larger/multi-page documents are lazy. This item is the **large/lazy** half,
deferred past the images PR that accepts the ADR.

Current state this extends:
- `OcrController::onVisiblePageChanged` (`src/ui/OcrController.cpp:53-108`) today
  OCRs the visible page **± 1** neighbour (`:90-107`), and only for
  **non-large** docs — `isLargeDoc()` is `pageCount() > 50`
  (`OcrController.cpp:47-52`, `kLargeDocPageThreshold = 50` at
  `OcrController.h:125`); a large doc gets **no** ambient OCR at all
  (`:79-83`). This item widens the window to ± 2, brings the lazy window to
  large/multi-page docs (recenter-on-jump instead of the current
  cancel-to-nothing), and adds the OCR-as-scanned path for search/replace.
- Priorities and gates already exist: `MlPriority::VisiblePage`/`Prefetch`/`Idle`
  (`src/ml/MlScheduler.h:32-44`); battery/background suppression
  (`MlScheduler.h:81-97`, `MlScheduler.cpp:65-90,256-262`); `[ml.scheduler]`
  settings (`src/settings/Settings.h:134-141`).

Grounded by `docs/decision-records/0012-image-ocr-pipeline-lazy-window-bounded-cache.md`
(§G12.3). The ± 2 window and N = 2 are hand-tuned values ratified there; a change
needs the reopen evidence the ADR names.
