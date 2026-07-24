---
id: 2026-07-24-wine-uat-failures-triage
title: Triage the 17 Wine UAT failures surfaced by nightly.yml's Windows lane
priority: TBD
status: open
source: nightly.yml bootstrap run 30104846942 (PR #121), Windows lane, job 89523510316 — first-ever UAT-under-Wine execution
created: 2026-07-24
---

## Threshold

Each of the 17 tests listed below either (a) passes under Wine, or (b) carries
a documented per-test Wine `QSKIP` with a stated rationale (the pattern
`docs/backlog/2026-07-19-wine-cross-thread-editor-save.md` and
`2026-07-21-wine-keep-restore-file-move-open-handle.md` already use for
Wine-only unit-test artifacts). Once every item is in one of those two
states, the nightly release table's Wine UAT count (`✅`/`⚠️ N/40`) reflects
that outcome and this item closes.

## Context

`nightly.yml` (PR #121) is the first place Trailer's UAT suite has ever run
under Wine — `ci.yml` and `release.yml`'s existing Wine lane explicitly
excludes the `uat` label. Bootstrap run
[30104846942](https://github.com/programmerq/trailer/actions/runs/30104846942),
Windows lane, job
[`89523510316`](https://github.com/programmerq/trailer/actions/runs/30104846942/job/89523510316):

- **Wine unit tests: 57/57 passed (100%), ~57s total.** Strong signal Wine
  itself is not fundamentally broken for Trailer's harness.
- **Wine UAT: 23/40 passed (58%), 17 failed.**

Per the owner's 2026-07-24 decision, Wine UAT is **non-gating** for
`nightly.yml` — a Windows lane with a green build + Wine unit pass still
stages and publishes its artifact regardless of the Wine UAT count. It's
surfaced instead as visible signal in the nightly release body's per-OS
table (e.g. `⚠️ Wine UAT 23/40`) so the count is trackable night to night.
Wine UAT becomes gating once a real Windows CI runner exists — Wine is a
stand-in for Windows here, not the platform Trailer ships to (see
`nightly.yml`'s "UAT suite (Wine)" step comment).

The 17 failing tests, from `ctest`'s summary in `build-win/`:

```
test_uat_foundations
test_uat_search_and_markup
test_uat_password
test_uat_autofill
test_uat_background_removal
test_uat_recognize_text
test_uat_external_change
test_uat_ml_affordances
test_uat_pdf_pages
test_uat_page_change_signal
test_uat_two_page_dpr1
test_uat_two_page_dpr1_5
test_uat_two_page_dpr2
test_uat_empty_state
test_uat_file_menu_ia
test_uat_zoom_indicator
test_uat_empty_state_recent
```

**Worth checking first:** `test_uat_foundations` and `test_uat_search_and_markup`
both ran concurrently (`CTEST_PARALLEL_LEVEL=2`) and both failed at almost
exactly the same wall-clock — 92.98s and 93.00s respectively — with **no**
captured output despite `--output-on-failure`. That near-identical duration
smells like a shared timeout/watchdog under Wine rather than two independent
assertion failures, and might explain a cluster of the 17 rather than each
needing separate diagnosis. The other 15 failed at a spread of individual
durations (1-32s), which reads more like genuine per-test issues — possibly
related to, but not necessarily the same mechanism as, the two already-
tracked Wine cross-thread-handle artifacts in
`docs/backlog/2026-07-19-wine-cross-thread-editor-save.md` and
`docs/backlog/2026-07-21-wine-keep-restore-file-move-open-handle.md` (those
are unit-test-specific findings, not UAT, so the connection is unconfirmed).

No root-causing or fixing was attempted here — this item exists to track the
finding for the owner to work through directly, per their stated intent
("happy digging on the failures when I'm back at my laptop").
