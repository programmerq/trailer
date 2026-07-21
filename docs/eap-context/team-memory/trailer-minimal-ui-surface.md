---
name: trailer-minimal-ui-surface
description: Owner standing UX directive (2026-07-21, PR #104 review) — minimal UI surface; the document/content is always the main focus. Prefer subtle in-context status affordances (state glyphs on the relevant menu entry/control, inline hints) over dialogs, popups, and progress bars. A recurring failure mode has been defaulting to old-style dialog/popup/progress-bar UI.
metadata:
  type: feedback
  modified: 2026-07-21T13:16:36.508Z
---

Owner directive on PR #104 (2026-07-21), rejecting a background-removal PROGRESS BAR in favor of a status GLYPH on the Remove Background menu entry. Verbatim: "A very subtle UI hint is much better than long-form text and progress bars. The document should always be the main focus." And: "A common theme so far has been to default with older style dialog/popups/progress bars/etc. We want a minimal UI surface."

**Why:** the content/document is the product; chrome that narrates progress or pops modals pulls focus and reads as dated. The owner has flagged this as a RECURRING default to correct, not a one-off.

**How to apply:**
- Show operation status as a subtle in-context affordance — a state glyph on the triggering control/menu entry (available / calculating / unavailable / failed), or an inline hint — NOT a progress bar or foreground dialog. Keep long ops async so the document stays interactive and focused.
- Reserve modal dialogs for genuine user DECISIONS (unsaved-changes confirm, destructive-action confirm). Never to narrate the user's own action, a no-op, or mere progress (see [[trailer-no-narration-dialogs]]).
- When you catch yourself reaching for a dialog/popup/progress bar, stop and find the subtle in-context alternative first.
- The project design guidelines doc (added via the claude/design-ux-guidelines PR, 2026-07-21) is the canonical write-up; this memory is the standing rule. Related: [[trailer-no-narration-dialogs]], [[trailer-ux-evidence-ruling]], [[trailer-review-before-push-policy]].
