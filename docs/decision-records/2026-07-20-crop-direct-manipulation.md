# Direct-manipulation page crop (drag-on-page with live preview) + all-zero-margin feedback

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-20
- **Date accepted / superseded:** 2026-07-20 (accepted)

## Context

The user-facing behaviour in question: **how a user crops PDF pages, and what
happens when they commit an all-zero crop.** This record covers the two
behaviours the accepted backlog item
[`docs/backlog/2026-07-15-crop-pages-direct-manipulation.md`](../backlog/2026-07-15-crop-pages-direct-manipulation.md)
names in its threshold. That item is the ratified requirement; this record
pins the specific interaction the implementation commits to (per G6, so the
user-visible change carries an accepted record beyond ADR-0012's scope —
ADR-0012 explicitly deferred "the deeper Crop redesign (drag-on-page with live
preview)" to this backlog item and this record).

**What ships today (before this change):** `Tools → Crop Pages…` opens a modal
(`src/ui/CropPagesDialog.*`) with four millimetre spin-boxes and **no preview**;
the user guesses numeric margins with no visual reference and commits blind
(`MainWindow::onCropPages`). An all-zero OK returns early with **no feedback**
(`if (l == 0.0 && t == 0.0 && r == 0.0 && b == 0.0) return;`), so an accidental
all-zero commit looks like nothing happened. There is no way to crop by
gesture.

**What this record ratifies:**

1. A new `Tools → Crop Pages by Dragging` action activates an on-page crop
   tool. The user drags a rectangle over the region to keep; the area outside
   dims to a **live preview**; corner handles adjust it; `Enter` commits,
   `Esc` cancels. No numeric dialog is opened. The rectangle is stored in
   **document space** (page points), so it is page-anchored and dpr-safe by
   construction (the #91/#94 zoom-drift invariant). The crop tool **owns the
   pointer**: a press never selects an annotation underneath (owner ruling
   2026-07-20).
2. The numeric dialog's all-zero commit now **flashes a status message**
   ("No crop applied — all four margins were zero.") instead of silently
   no-opping.

The numeric dialog is **retained** for users who want exact margins — this is
"in addition to", not "instead of" (the backlog item allows either).

Relevant sources: PHILOSOPHY → *How Trailer reduces friction* and *No lying
controls*; DESIGN §6 crop; the shared overlay coordinate plumbing in
`src/document/PdfAdapter.cpp` (`setViewToDocument`) and
`src/ui/AnnotationOverlay.cpp`; the CropBox mutation in
`src/document/PdfCommands.cpp` (`CropPageCommand`).

## Options

- **A. Add drag-on-page crop with live preview + fix all-zero feedback; keep
  the numeric dialog.** What ships. Gesture is primary; numeric stays for
  precision. All-zero OK explains itself.
- **B. Replace the numeric dialog entirely with drag crop.** Drops exact-margin
  entry — a regression for users who want repeatable numeric crops.
- **C. Leave crop numeric-only; just fix the all-zero feedback.** Meets only
  half the backlog threshold; the direct-manipulation friction stays.

## Personas debate

- **Office non-technical user:** Wants to "cut off the scanner's black edge".
  Dragging a box over the keep-region is the obvious gesture; guessing
  millimetres is not. Favours A/B; the live dimmed preview is what makes it
  legible. A over B because it doesn't take anything away.
- **Older careful user:** Wants to know exactly what a commit will do. The
  dimmed preview + Enter-to-commit (not commit-on-release) means nothing
  happens until they confirm; `Esc` backs out. The all-zero feedback removes a
  "did that do anything?" moment. Favours A.
- **Power migrator:** Preview/Acrobat both crop by dragging a rectangle with a
  live darkened preview and handles; a numeric-only crop reads as primitive.
  Favours A; keeping numeric-for-precision matches Acrobat's dual affordance.
- **Occasional user:** May pick the drag action on an image by reflex. A keeps
  the item disabled with a tooltip pointing at Crop Image (no dead end).

## Admissible objections

- **Power migrator, Option B, "replace the dialog":** removing numeric entry
  breaks repeatable margin crops (e.g. trimming the same 12pt gutter across
  documents). Decisive against B.
- **Older careful user, Option C:** numeric-only leaves the exact friction the
  backlog item was filed for — no visual reference for the crop region.
  Decisive against C.
- **Occasional user, Option A (the disabled-on-image case):** if the drag
  action were merely hidden or, worse, active-but-inert on an image, the user
  would hit a no-op. Answered by G3: the action is `setEnabled(false)` with a
  tooltip that routes image users to `Tools → Crop Image`.

### Rejected as naked preference

- "Commit the crop on mouse-release instead of on Enter." — rejected: states no
  concrete user/step/failure; and commit-on-release forecloses the *adjust
  before committing* step the threshold's "live preview before committing"
  calls for. Enter-to-commit is the deliberate choice, not an oversight.

## Checkable threshold this record establishes

Independently checkable; proven by `tests/test_crop_direct_manipulation.cpp`
and harness slot `uat_pdf_058_dragCropAppliesEndToEnd`
(`tests/uat/test_uat_pdf_pages.cpp`):

- With the crop tool active, a drag produces a crop rectangle whose
  **document-space** coordinates are **invariant across zoom ∈ {1.0, 1.5, 2.0}
  and devicePixelRatio ∈ {1, 1.5, 2}** (page-anchored, dpr-safe).
- A crop-tool press over an existing annotation does **not** select it
  (`selectedAnnotationId() == 0`, store unchanged) — crop owns the pointer.
- `Enter` commits and shrinks the page /CropBox end-to-end via `cropPage`;
  the crop is a single undo step; `Esc` cancels with no change.
- The numeric dialog's all-zero OK produces a visible status message and does
  not mark the document dirty.

## Arbiter verdict + rationale

**Accepted 2026-07-20 — Option A.** It is the direct, derivable implementation
of the accepted backlog item's threshold under the ratified
friction-reduction and no-lying-controls principles, and it mirrors the
Preview/Acrobat norm the power-migrator lens expects. Option B is rejected by
the power-migrator objection (loses numeric precision); Option C by the
older-careful/occasional lenses (meets only half the threshold). The record is
marked accepted rather than proposed because every choice it commits to is
derived from an already-accepted item plus PHILOSOPHY — the same basis on which
ADR-0011 and ADR-0012 were accepted the day they were drafted — and it forks no
question the owner has not already settled.

The implementing constants carry their in-code rationale: the minimum crop side
`kMinCropSideDoc` (`src/ui/AnnotationOverlay.cpp`) and the preview scrim alpha
(`src/ui/AnnotationOverlay.cpp`, paintEvent crop block).

## Evidence required to reopen

A measured case where Enter-to-commit or the dimmed-preview model demonstrably
harms a real crop flow (e.g. a usability finding that users expect
commit-on-release), or a decision to drop the numeric dialog (which would need
a repeatable-margin replacement), plus owner sign-off.
