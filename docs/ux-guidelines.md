# Trailer — UX Design Guidelines

> **Why this exists.** A recurring theme across sessions has been to reach
> for the older-style dialog / popup / progress-bar first — and, just as
> often, to add chrome that re-announces state the document already shows,
> or to let a control drift position as a side effect of something
> unrelated. This document is the durable, principle-first counter:
> **Trailer's UI surface stays minimal, and the document is always the
> main focus.** When a change adds a user-visible affordance, hold it
> against the rules here before you build it.

This consolidates and points at the UX rules already scattered across the
tree — it does not replace them:

- The *reasoning* lives in [`../PHILOSOPHY.md`](../PHILOSOPHY.md) →
  *How Trailer reduces friction*.
- The *pass/fail merge gates* live in [`../AGENTS.md`](../AGENTS.md) →
  *Hard gates* (G2 UX-Done screenshots, G3 No lying controls, **G10
  Deference and spatial constancy** — the gate this document backs).
- The *ML-specific convention* (status, not a modal) lives in
  [`CONVENTIONS.md`](CONVENTIONS.md) §12.
- Per-decision precedent lives in
  [`decision-records/`](decision-records/) (e.g. ADR-0002, ADR-0011).

---

## The two principles

Trailer's UI is judged against two distinct properties. A change can pass
one and fail the other — they're named separately on purpose, so "I fixed
the chrome" doesn't get credited for a layout that still moves.

**Deference** (Apple HIG's own term): the interface helps the user
understand and interact with content but never competes with it. Chrome
that reports state the user can already see on the document itself — a
zoom percentage sitting in the status bar permanently, a "Recovery
Snapshot Saved" toast for background autosave nobody asked about, a
sidebar labelled "Sidebar" — fails deference. None of these *lie* (that's
G3's territory); they're just chrome that shouldn't be permanent. The
broader tradition this sits in is **calm technology** (Weiser & Seely
Brown): information stays in the periphery until it earns the user's
attention, rather than demanding it up front.

**Spatial constancy** (positional stability): controls do not move as a
side effect of unrelated state changes. This is a *different* failure
mode — a toolbar can add zero new chrome and still fail spatial constancy
by reflowing, both vertically and horizontally, because a sibling
toolbar's visibility toggled; a view-mode menu can be perfectly honest
about its items and still fail by reordering them depending on which mode
is active. Fixing deference does not fix spatial constancy, and vice
versa — check both, separately.

The house phrase for both: **"The document is the subject; the furniture
doesn't move."**

These two are gate **G10** in [`AGENTS.md`](../AGENTS.md) — see that entry
for the exact Rule / Test / Evidence a PR is checked against, and its
explicit boundary against G3. This document is the detail behind the
gate, not a second, softer standard.

The reference user opens a file, does the thing, and closes the file.
Every pixel of chrome between them and the document is a tax on their
attention. Prefer the affordance the user can ignore until they need it
over the one that demands acknowledgement — a very subtle in-context hint
beats long-form text and a progress bar, every time.

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

## Recognize the anti-pattern: real violations

Abstract principles get lawyered; concrete examples get followed. These are
real findings from a single review pass — the shape of the bug to watch
for, not an exhaustive list:

- **A zoom percentage pinned in the status bar at all times.** The document
  already shows its own scale; a permanent `100%` readout is chrome
  reporting internals nobody asked to monitor. *Deference.*
- **A "Recovery Snapshot Saved" toast for background autosave.** Narrates a
  success the user didn't ask about and can't act on (compare PHILOSOPHY →
  *A popup is a last resort* / *Dialogs are for decisions, not
  narration*, above). *Deference.*
- **A sidebar labelled "Sidebar."** A label that describes the chrome
  instead of what it contains is the chrome announcing itself. *Deference.*
- **Toolbars reflowing — vertically and horizontally — when a sibling
  toolbar's visibility is toggled.** The main toolbar should stay exactly
  where it was, whether or not the other one is shown. *Spatial
  constancy.*
- **View-mode menu items reordering themselves based on the active mode.**
  Nothing here is dishonest — every label is accurate — but the user's
  muscle memory for a menu position breaks anyway. *Spatial constancy.*

## Quick self-check before adding UI

1. Can this be a glyph, badge, tooltip, or status-bar item on the control
   it concerns, instead of new chrome?
2. If it's a dialog — does it collect a *decision*, or is it narrating?
   (If narrating, delete it.)
3. Does the document stay the visual and interactive focus while this is
   on screen?
4. Does long-running work stay async so the page never freezes?
5. **Is what you're adding permanent chrome that reports state the
   document already shows?** If so, make it on-demand instead (tooltip,
   hover, menu entry) rather than always-on. (G10 — deference)
6. **Does any existing control change on-screen position as a side effect
   of unrelated state** — a toolbar reflow when a sibling toggles, a menu
   reordering by mode? (G10 — spatial constancy)
7. Have you captured the affected states for G2, including the quiet
   ones (glyph shown / hidden, disabled + tooltip)?

If a change can't satisfy these, treat it as a stop-and-discuss item in
the PR before building — not a drive-by.
