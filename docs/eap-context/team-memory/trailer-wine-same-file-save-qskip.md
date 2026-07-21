---
name: trailer-wine-same-file-save-qskip
description: Wine-only CI gotcha — same-file PDF Save (open→annotate→Save over the source) fails under the Wine unit-test tier because a worker-thread-parsed qpdf editor's file handle isn't released on close during QFile::remove; resolved by a runningUnderWine() QSKIP on those cases (Linux keeps all assertions), precedent set by PR #90 (2026-07-20)
metadata:
  type: project
---

# Same-file PDF Save fails under Wine — documented QSKIP precedent (PR #90)

While landing the P0 discard-file-integrity fix (PR #90, branch `claude/discard-file-integrity-421127b`, final SHA `5c0046b`), the new regression test failed **only** on the Windows-cross + Wine CI tier ([[trailer-windows-cross-wine-ci]]) across ~5 rounds. Chasing it via in-CI unbuffered `[chk]` stderr checkpoints (Wine is NOT reproducible in the agent container) established ground truth:

- **Not a crash** — `PdfDocument::save()` returns `false` (rc=2, clean assertion failures) in the two cases that open → annotate → Save **over the same source file**.
- **Root cause:** the background annotation-sweep parses its qpdf editor on a **worker thread**; that editor is adopted as the GUI editor; when the same-file Save then closes it on the main thread during the `QFile::remove` step, **Wine won't release the worker-thread's file handle**. Real Windows makes qpdf file handles process-global, so this is a **Wine-emulator artifact, not a product bug** (Linux passes every assertion).
- A thread-pinning test seam (`loadEditorSyncForTesting`) did **not** fix it and was removed.

**Resolution (the precedent):** gate just those two same-file-Save cases behind a `runningUnderWine()` `QSKIP` (detected via `ntdll!wine_get_version`) with a documented reason comment; **all assertions still run on Linux** (and would on native Windows). Follow-up to verify on a native-Windows box is tracked in `docs/backlog/2026-07-19-wine-cross-thread-editor-save.md`. This mirrors the existing precedent that UAT is excluded from the Wine tier.

## Debugging-loop lessons (for the next Wine-only failure)
- Wine isn't reproducible locally — don't blind-push speculative fixes. Instrument with **unbuffered** (`setvbuf(stderr,…,_IONBF,0)`) `[chk]` checkpoints, push once, and read the LAST checkpoint before the failure. A failing `QVERIFY` returns before its trailing `:done`, so a printed `:done` per case == that case's assertions passed.
- A **queued** Actions run during a GitHub incident is the outage, not a failure — do NOT re-push/re-trigger/diagnose runners; back off to a long-interval `send_later` re-check.
- Two genuine latent bugs were flushed out en route and fixed in the same PR: a `QPDFWriter` dangling-filename use-after-free (4 sites) and a recover/save annotation duplication.

Related: [[trailer-windows-cross-wine-ci]], [[trailer-dirty-close-fix-merged]], [[trailer-verify-remote-after-push]].
