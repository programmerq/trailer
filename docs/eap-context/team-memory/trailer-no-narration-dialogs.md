---
name: trailer-no-narration-dialogs
description: Owner taste rule (2026-07-16 dogfood): never show dialogs that merely narrate what the user just did or that nothing happened — user-cancel is silent, non-image paste is a noop; status-bar-only for passive info
metadata:
  type: project
---

During 2026-07-16 mac dev-build dogfooding the owner rejected two informational popups as "noise": the "screen capture cancelled" dialog after he cancelled a capture ("I don't need to be told that I canceled the action after I cancel it") and a popup when pasting non-image clipboard data ("Paste from clipboard should be a noop if the clipboard data is not an image. The popup is just noise.").

**Why:** dialogs demand acknowledgment; narrating a no-op or the user's own action costs a click and delivers zero information. Fits the existing status-bar-only feedback convention (CONVENTIONS #12, ADR 0002 precedent).

**How to apply:** user-cancel paths are silent (at most a transient status-bar note). Actions that can't apply (non-image paste) are disabled-with-tooltip or silently noop — never an informational modal. Reviewers/personas should flag any new QMessageBox that only reports what just happened. Related: [[trailer-requirements-summary]], [[trailer-ux-evidence-ruling]].
