# UAT — Security (Phase 5)

Tests in this section exercise password protection, permissions, and
related features introduced in Phase 5.

---

## Password-protected PDF

### UAT-SEC-010 — Open a password-protected PDF

**Preconditions:** A PDF encrypted with a known user password is available.
**Steps:**
1. `File > Open…` and select the encrypted PDF.
**Expected:**
- A password dialog appears with the filename in the prompt.
- Entering the correct password loads the document normally (pages visible,
  search and annotation tools available).

### UAT-SEC-011 — Wrong password does not crash

**Preconditions:** An encrypted PDF is open via the dialog.
**Steps:**
1. Enter an incorrect password three times.
**Expected:**
- After three failed attempts the app gives up prompting and opens the
  document in a locked state (shows "Could not open" in the content area).
- No crash or assert.

### UAT-SEC-012 — Cancel password dialog

**Preconditions:** A password dialog is showing.
**Steps:**
1. Click Cancel.
**Expected:**
- The file loads in locked state (same as wrong-password). No dialog
  lingers. App remains responsive.

### UAT-SEC-013 — Recent files remembers encrypted path

**Preconditions:** An encrypted PDF was previously opened successfully.
**Steps:**
1. Restart the application.
2. Open the file via `File > Open Recent`.
**Expected:**
- The password dialog appears again (the password is not cached).
- Entering the correct password opens the document normally.

---

## Export as Password-Protected PDF

### UAT-SEC-020 — Export writes an encrypted file

**Preconditions:** A plain PDF is open.
**Steps:**
1. `File > Export as Password-Protected PDF…`
2. Choose a destination path.
3. Enter a password and confirm it.
4. Click OK.
**Expected:**
- An encrypted PDF is written to the chosen path.
- Opening the exported PDF prompts for the password.
- Entering the password loads the document; page count matches the original.

### UAT-SEC-021 — Mismatched passwords are blocked inline, destination kept

**Preconditions:** A plain PDF is open.
**Steps:**
1. `File > Export as Password-Protected PDF…`
2. Choose a destination path.
3. Enter different strings in Password and Confirm.
**Expected:**
- The OK button is disabled and an inline hint reads "Passwords do not
  match." (hovering the disabled OK shows the same reason). No warning
  dialog appears; no file is written.
- Correcting the Confirm field to match enables OK immediately, **without
  reopening the Save dialog** — the destination chosen in step 2 is kept.

### UAT-SEC-022 — Empty password is blocked inline

**Preconditions:** A plain PDF is open.
**Steps:**
1. `File > Export as Password-Protected PDF…`
2. Choose a destination path.
3. Leave both password fields empty.
**Expected:**
- The OK button is disabled and an inline hint reads "Enter a password to
  protect the PDF." No warning dialog appears; no file is written.
- Typing a password and a matching confirmation enables OK without
  reopening the Save dialog.

Regression guard: `tests/test_password_export_dialog.cpp` covers the
validation state machine (empty / confirm-empty / mismatch / valid) and that
the OK button's enabled state and tooltip track validity live.

### UAT-SEC-023 — Export preserves existing annotations

**Preconditions:** A PDF with at least one annotation (rectangle, line, etc.)
has unsaved markup. Use `File > Save` first, then export.
**Steps:**
1. Draw a rectangle.
2. `File > Save`.
3. `File > Export as Password-Protected PDF…`
4. Choose a path, enter a password, click OK.
5. Open the exported PDF and enter the password.
**Expected:**
- The rectangle annotation is visible in the exported PDF.

---

## Permissions (future)

These cases are deferred to Phase 5 polish once the Permissions dialog
is added. The underlying `EncryptionOptions` struct already carries the
permission flags; the UI to expose them is pending.

- **UAT-SEC-030** Print restriction — exported PDF respects `allowPrint = false`.
- **UAT-SEC-031** Copy restriction — exported PDF respects `allowExtract = false`.
- **UAT-SEC-032** Modify restriction — exported PDF respects `allowModify = false`.
