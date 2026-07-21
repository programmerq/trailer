# Trailer — UX Design Guidelines

> **Why this exists.** A recurring theme across sessions has been to reach
> for the older-style dialog / popup / progress-bar first. This document
> is the durable, principle-first counter: **Trailer's UI surface stays
> minimal, and the document is always the main focus.** When a change adds
> a user-visible affordance, hold it against the rules here before you
> build it.

This consolidates and points at the UX rules already scattered across the
tree — it does not replace them:

- The *reasoning* lives in [`../PHILOSOPHY.md`](../PHILOSOPHY.md) →
  *How Trailer reduces friction*.
- The *pass/fail merge gates* live in [`../AGENTS.md`](../AGENTS.md) →
  *Hard gates* (G2 UX-Done screenshots, G3 No lying controls).
- The *ML-specific convention* (status, not a modal) lives in
  [`CONVENTIONS.md`](CONVENTIONS.md) §12.
- Per-decision precedent lives in
  [`decision-records/`](decision-records/) (e.g. ADR-0002, ADR-0011).

---

## The one principle

**The document is the main focus; the UI recedes.** The reference user
opens a file, does the thing, and closes the file. Every pixel of chrome
between them and the document is a tax on their attention. Prefer the
affordance the user can ignore until they need it over the one that
demands acknowledgement.

A very subtle in-context hint beats long-form text and a progress bar,
every time.

## Prefer in-context affordances over chrome

When a feature has state to show or a signal to surface, put it **on the
thing it concerns** and let the user glance past it:

- A **state glyph / badge on the relevant control or menu entry** — not a
  banner, not a popup. (Precedent: `MainWindow::updateRemoveBackgroundBadge`
  marks the *Remove Background* entry when the current image is a good
  candidate.)
- An **inline hint or selectable layer painted on the page** — output
  lands in the document, not in a dialog. (OCR text becomes a selectable
  layer over the page; it is never dumped into a message box.)
- A **quiet ambient indicator** for background work — a small status-bar
  item, not a foreground bar. (Precedent: the `m_mlIndicator` status-bar
  label; per CONVENTIONS §12 this is the *only* affordance the user sees
  for background ML.)

The recurring direction of travel is **dialog → in-place, never the
reverse.**

## Long-running work: show state, keep the document live

- Never freeze the document behind a modal `QProgressDialog`. ML work runs
  async through `Application::mlScheduler()` (CONVENTIONS §12); the page
  stays interactive and focused while it runs.
- A subtle indicator that the work is happening is enough. Reserve a
  determinate percent-done treatment for the rare operation that genuinely
  blocks the user's next action *and* runs long enough to need it — and
  even then, prefer a non-modal placement. (See ADR-0002 for how progress
  and cancel were settled for background removal.)
- If a result comes back unusable, **drop it and let the user retry** —
  don't pop a dialog to narrate the failure. (PHILOSOPHY → *A popup is a
  last resort*.)

## Dialogs are for decisions, not narration

A modal interrupts. Spend that interruption only on a genuine **user
decision** the user would not want made implicitly.

**A dialog IS warranted when:**

- The user must make an up-front choice the app can't infer — a file
  picker, a page range, export parameters.
- An action is destructive or irreversible and needs confirmation —
  unsaved-changes on close, a redaction warning.
- One-time consent is required — an ML model download.
- An error can't be made self-evident from UI state.

**A dialog is NOT warranted to:**

- Narrate the user's own action back to them ("Background removed").
- Report a no-op or a "nothing to do here."
- Explain why a control is unavailable — that is a **disabled control with
  a tooltip**, per G3 and PHILOSOPHY → *No lying controls*. A popup that
  just says "no" is noise the user must dismiss to continue.
- Show mere progress (see above).

This is the existing **no-narration** norm, stated positively: if the
popup would only tell the user something they just did or could already
see, it shouldn't exist.

## Quick self-check before adding UI

1. Can this be a glyph, badge, tooltip, or status-bar item on the control
   it concerns, instead of new chrome?
2. If it's a dialog — does it collect a *decision*, or is it narrating?
   (If narrating, delete it.)
3. Does the document stay the visual and interactive focus while this is
   on screen?
4. Does long-running work stay async so the page never freezes?
5. Have you captured the affected states for G2, including the quiet
   ones (glyph shown / hidden, disabled + tooltip)?

If a change can't satisfy these, treat it as a stop-and-discuss item in
the PR before building — not a drive-by.
