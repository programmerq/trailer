---
name: trailer-pr113-two-page-revised
description: PR #113 (two-page/spread view) fuller-revise COMPLETE 2026-07-21 — 8 ratified items + 2 full reviews + focused re-review, all findings fixed; remote tip 5890298, ready-for-review draft, stacked on #112 (CI zero-by-design), merge owner-gated
metadata:
  type: project
  modified: 2026-07-21T13:17:12.310Z
---

PR #113 `claude/two-page-view-pr1`, two-page (spread) viewer à la macOS Preview. Owner's 3 critiques → adversarial trio → coordinator ratified a FULLER revise; executed 2026-07-21 as checkpoint-pushed items on top of 5805996:
- Item 1 mode banner ("Two Pages is a read-only view — switch to Single or Continuous to edit") as primary read-only signal + form-toolbar tooltip fix (fills forms). Item 7 DESIGN §6.1.2 ratified-shape note.
- Item 5 live sidebar page tracking during free-scroll (`TwoPageView::currentPageChanged`). Item 6 spread-aware Fit-Width/Fit-Page IMPLEMENTED (not degraded; `fitWidthZoom()/fitPageZoom()`).
- Item 3 UAT vwr_079 zoom-readout==painted-render-scale (negative-control verified — the original "100%" shot was really ~50%). Item 4 UAT vwr_080 dpr backing-resolution oracle {1,1.5,2} (anti-Retina-blur). Item 2 honest evidence re-shoot with a book fixture, short-named `docs/uat/images/tp-{before,after,zoom100,disabled}.png`, SHA-pinned inline <img> (pinned to 6d7658e). Item 8 PR2 deferrals + backlog: cover-alone toggle, search-in-two-up, ctrl+wheel zoom.

Review round (owner policy): hardliner persona = all G-gates PASS; PDF-UX persona = 1 BLOCKER (Next Page stuck at 2nd spread because currentPage() returned the spread leading page and goToPage(currentPage()+1) stayed in-spread). Fixed by spread-aware Next/Prev via new `IDocument::nextPageIndex()/previousPageIndex()` virtuals consulting `TwoPageView::leadingPageOf{Next,Prev}Spread()`; guard uat_vwr_081. Focused re-review found 1 should-fix (midpoint tracking skipped spreads >2× viewport tall) → fixed with top-crossing rule; guard uat_vwr_082. All negative-control verified.

State: remote tip `58902984cedf55cddb6e20c1338afc5ceddf87b1` (ls-remote verified), full UAT 29/29 + unit 58/58 green, PR is a ready-for-review DRAFT, stacked on #112 so its CI shows zero runs by design, merge owner-gated.

PENDING REASSESSMENT (2026-07-21, teammate taste-signal): the owner is pivoting toward a minimal-UI-surface rule — subtle in-context status, NO dialogs, no long-form text, the document stays the focus (a guideline PR + standing rule is being landed via the #104 ML-progress session). The #113 mode banner ('Two Pages is a read-only view — switch to Single or Continuous to edit') is in-context but TEXT-FORWARD and may fall under this taste. Do NOT rework preemptively. When the minimal-UI guideline PR lands (or the owner rules on the banner in #113 review), assess the banner against its exact wording and either (a) justify it in #113's body — a mode-STATE banner may be legitimately different from operation-PROGRESS chrome — or (b) slim it to a compact badge near the mode control with the detail moved to a tooltip. The per-control disabled tooltips are the G3 floor and stay regardless.

Related: [[trailer-review-before-push-policy]], [[trailer-ux-evidence-ruling]], [[trailer-verify-remote-after-push]], [[trailer-pr-inline-image-proxy-defang]].
