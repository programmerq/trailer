---
name: trailer-followup-docket
description: SUPERSEDED — migrated to in-repo docs/backlog/ via PR #51 (2026-07-12). Consolidated ranked follow-ups harvested 2026-07-10 from the four archived work sessions (empty-state, criteria-gates, affordances, preferences)
metadata:
  type: project
---

**SUPERSEDED 2026-07-12 — the live backlog now lives IN-REPO at `docs/backlog/` (one item per file, id `YYYY-MM-DD-<slug>`; see `docs/backlog/README.md`). All still-open items from this docket were migrated there and 3 new owner items added via PR #51 (https://github.com/programmerq/trailer/pull/51, branch docs/backlog-process, remote SHA 07bac55). Do NOT add new follow-ups here — add a file under `docs/backlog/`. This memory is retained only as the migration provenance/audit trail.**

Harvested at archive time per owner's collect-perspectives practice. Grouped, deduplicated, ranked. Thresholds were declared per item in each session's harvest (see those transcripts for full wording).

**P0 — data safety — RESOLVED 2026-07-12 (fixed + merged via PR #47, commit f0f76ad):** closing the last tab of a dirty document silently discarded edits (UAT-FND-014; DocumentView::onTabCloseRequested erased the doc before allTabsClosed, so closeEvent's dirty-check saw zero docs). Fix: synchronous veto signal DocumentView::documentCloseRequested → reusable MainWindow::confirmCloseDirtyDoc (extracted from closeEvent); now covers BOTH close-last-dirty-tab and close-non-last-dirty-tab (both were silently discarded; window-close path already prompted). Six UAT-FND-014 regression cases added; full suite 47/47 green, uat 15/15. Satisfies never-worry-save no-silent-loss invariant; ADR 0004 flipped proposed→accepted 2026-07-12. Details: [[trailer-dirty-close-fix-merged]]. This was the top-ranked follow-up; with it closed the next-ranked open item is the P1 gate-enforcement layer below (items a/c/d still open; b DONE).

**P1 — stand up the gate enforcement layer (criteria session):** (a) reference rig spec + checked-in docs/perf/corpus/ (≥3 files) + written G2 ruling on offscreen grab() — makes budgets B1–B4 binding; (b) drive scorer-0.50 ADR to `accepted` — DONE 2026-07-12: ADR 0003 ACCEPTED via the arbiter machinery (20 pages / 3 fields ratified, owner veto retained); (c) run gate G5 once against the real app (per-OS empty-state screenshots) to prove the evidence artifact is producible; (d) platform command-surface/shortcut tables folded into DESIGN (rescue commit requested before archive — verify it landed).

**P2 — regression-proofing conventions (affordances):** central makeDisabledAction helper + test asserting every menu with disabled+tooltip actions has toolTipsVisible() — DONE 2026-07-12 (merged in PR #48, head 3ff629a → main): makeDisabledAction helper couples tooltip with host setToolTipsVisible(true), generalized guard test uat_fnd_043; enum-switch no-fallthrough convention (-Wswitch, no default) for mode mappings — DONE 2026-07-12 (PR #48): scoped -Werror=switch in cmake/CompilerWarnings.cmake, convention docs/CONVENTIONS.md §14; headless regression test for the untitled-doc close-save path + non-blocking progress for large saves (still open).

**P2 — settings infrastructure (preferences):** Settings live-vs-restart key registry; restart-only keys show a restart hint; test that live keys apply without restart — DONE 2026-07-12 (merged in PR #48, head 3ff629a → main): Settings::Volatility/volatilityOf/SettingsKeys registry (all keys classified Live today), Preferences shows a "Requires restart" hint for any future RestartRequired key, completeness + live-apply + hint tests, convention docs/CONVENTIONS.md §15.

**P3 — platform honesty:** Wayland screenshots via XDG portal or disabled+tooltip (never silent null); macOS reopen path needs real-hardware verification (dock activation → open panel exactly once).

**P3 — polish backlog:** branded empty-state graphic (replace stock QStyle icon); inline Open Recent list in empty state; wire theme live (light/dark/system) and enable the Theme control; grow Preferences tabs only as real settings appear (Forms tab gated on ≥1 wired Forms setting); dynamic Maximize/Restore label (or owner accepts static).

- share the AcroForm field-count parse between content-aware sidebar defaults and form-fill auto-enable (Copilot flag on #36, 2026-07-12: ContentAwareDefaults eagerly calls doc->formFields() even when the ≥20-page rule short-circuits; accepted as-is by coordinator default since it's a one-time open cost — the follow-up computes the count once and shares it). Threshold: exactly one formFields() parse per document open, verified by counter test.

- route the background-removal ML op through the new progress/cancel widget (disclosed in PR #49, 2026-07-12: mechanism is general; OCR-only today). Threshold: background removal shows the same B5-compliant progress + per-op-appropriate cancel semantics, verified by state test + screenshot.

- replace the ML page-scroll hint's 150ms poll with a real page-changed signal on IDocument (disclosed in PR #49, 2026-07-12). Threshold: zero polling timers for hint re-derivation; signal-driven update verified by test.

**P4 — big roadmap item:** real two-page layout cannot come from QPdfView (no two-up PageMode) — custom layout/paint layer; do not assume it's a toggle.

**Housekeeping:** reset `Claude <noreply@anthropic.com>` commit author on docs/design-criteria-gates before the batched PR; reconcile the build-recipe variants in team memory (DONE 2026-07-10 — single canonical [[trailer-remote-build-recipe]]); accessibility-checklist first real audit activates at the dogfood-default milestone (G8); ignore stale harvest warnings about runner OOM (fixed by owner's resize 2026-07-10).

**History scrub — DROPPED by owner 2026-07-12:** owner ruled "Don't worry about the commit scrubbing preference. You can drop that." Claude authorship/trailers/session URLs in history and PR comments are acceptable; no main rewrite planned. (Coordinator note: agents' own harness rules about not embedding model IDs in pushed artifacts still apply to new work.)

Owner decision package (open): grab() ruling, arbiter identity, reference rig + corpus naming, never-worry-save invariant vs opt-out, magic numbers (0.50 ready; ≥3/≥20 unbuilt), dogfood-default marker, perf budget ratification (B1–B8).
