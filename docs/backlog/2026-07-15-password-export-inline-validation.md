---
id: 2026-07-15-password-export-inline-validation
title: Password-protected export should validate inline and preserve the chosen destination
priority: P2
status: open
source: annoyed-end-user persona, 2026-07-15 friction audit
created: 2026-07-15
---

## Threshold

In the Export as Password-Protected PDF flow, the OK button is disabled until
both password fields are non-empty and equal, with an inline hint explaining
why (rather than a warning dialog that aborts). If a validation error still
occurs, the already-chosen save destination is preserved — the user is not
forced back through the file picker. Verified: mismatched/empty passwords
cannot be submitted (OK disabled + hint), and correcting them keeps the same
destination path without reopening the Save dialog.

## Context

`MainWindow::onExportPasswordProtected` (`src/ui/MainWindow.cpp`) runs the file
picker first (Step 1, ~`:2414`), then the password dialog (Step 2). On a
password mismatch or empty password it pops `QMessageBox::warning(...)` and
`return`s (`src/ui/MainWindow.cpp:2443-2453`), discarding the destination the
user already chose in Step 1. Re-invoking the command re-opens the Save-As
picker from scratch, so a simple typo in the confirm field costs the user the
entire file-picking step again.

Per PHILOSOPHY → *No popup that just says "no"* and *How Trailer reduces
friction*, the correct shape is inline validation: keep OK disabled with a
live hint until the two fields match and are non-empty, and never throw away
work the user has already done (the chosen destination) on a validation error.
This mirrors the empty-margin friction filed in
`2026-07-15-crop-pages-direct-manipulation`.

## Provenance

annoyed-end-user persona, 2026-07-15 friction audit. Code cert:
`src/ui/MainWindow.cpp:2443-2453` (warn-and-abort validation), destination
chosen at `:2414`.
