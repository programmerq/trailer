# 0012 — Hide the Crop Pages "Apply to all pages" checkbox on a single-page document

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15 (accepted)

## Context

The 2026-07-15 friction audit (annoyed-end-user + UX-expert lenses) flagged the
Crop Pages modal. The user-facing behaviour in question: **whether the "Apply to
all pages" checkbox should be shown on a document that has exactly one page.** A
control that governs "all pages" is meaningless when there is only one page —
there is nothing else to apply the crop to — so it is pure noise on that surface.
This is a direct sibling of the Recognize-Text page-range friction that ADR-0011
reconciled: an affordance that offers a choice which cannot matter in the current
context.

**What ships today (so this record isn't misread as describing an open gap):**
the checkbox is now **hidden when `pageCount() <= 1`**. The dialog is extracted
into `src/ui/CropPagesDialog.cpp`, and the visibility rule lives there:
`m_allPagesCheck->setVisible(m_multiPage)` with `m_multiPage = pageCount > 1`.
On a single-page document `applyToAllPages()` returns false unconditionally, so
the crop falls to the current-page path — which for a one-page document is the
sole page. Output is byte-for-byte identical to leaving the (checked-by-default)
box unchecked; only the inert control is removed. **Multi-page behaviour is
unchanged:** the checkbox is shown, checked by default, and `applyToAllPages()`
reflects it.

This record exists because the gate reviewer ruled that hiding a control on the
crop dialog surface is a user-visible dialog-surface change and must carry an
accepted decision-record before push, per the owner's conservative default of
recording even quick default-flips unless truly obvious.

## Options

- **A. Hide the checkbox on a single-page document.** Show it only when
  `pageCount() > 1`; on one page the crop targets the sole page directly. What
  ships. Removes a meaningless choice, output unchanged.
- **B. Leave the checkbox always visible.** Keep the "Apply to all pages" toggle
  on every document including single-page ones, inert-but-present.
- **C. Skip the record, land the flip as an obvious cleanup, or defer the whole
  thing to backlog.** The deeper Crop redesign (drag-on-page with live preview)
  is already backlogged; treat the checkbox as part of that.

## Personas debate

- **Annoyed end-user:** Opens a one-page scan to crop it, meets a checkbox
  offering to apply the crop to "all pages" of a one-page document. Reads it as
  the app not knowing what it's looking at. Favours A.
- **UX expert:** A control whose choice cannot change the outcome is noise that
  costs attention and teaches distrust of the dialog's other controls. Favours A;
  B is a textbook meaningless-choice.
- **Older careful user:** Wants to know exactly what a commit will do. Hiding a
  control that has no effect removes a false decision point without hiding any
  real behaviour — the crop still targets their page. Not harmed by A.
- **Power migrator:** Acrobat/Preview do not surface an "all pages" toggle on a
  one-page crop. A matches the native norm.

## Admissible objections

- **UX expert, Option B:** on a single-page document the toggle governs a set of
  one, so neither state changes the result — a meaningless choice at the "what
  does this box do?" step. Decisive against B.
- **Any user, Option C (skip the record):** the crop dialog is a user-visible
  surface; flipping a control's visibility without a record leaves no ratified
  trace of why the surface changed. Answered by writing this record even though
  the behaviour is output-identical — the conservative default governs.

### Rejected as naked preference

- "Keep the checkbox for consistency across documents." — rejected: names no
  user, step, or failure; consistency with a case where the control is inert is
  not a benefit, it is the meaningless choice named above.

## Checkable threshold this record establishes

Independently checkable, proven by `tests/test_crop_pages_dialog.cpp`:

- On a **single-page** document the "Apply to all pages" checkbox is **not
  visible** (`applyToAllCheckBox()->isVisible() == false`) and `applyToAllPages()`
  returns false, so the crop targets the sole page via the current-page path.
- On a **multi-page** document the checkbox **is visible**, checked by default,
  and `applyToAllPages()` tracks it — behaviour unchanged.

## Arbiter verdict + rationale

**Accepted 2026-07-15 — Option A.** The change applies the ratified PHILOSOPHY
friction-reduction / no-meaningless-choice principle (`PHILOSOPHY.md` → *How
Trailer reduces friction*): the output is identical to the pre-change unchecked
path, only a noise control is removed, and multi-page behaviour is untouched.
Option B is rejected as the meaningless choice the UX-expert objection names.
Option C is rejected two ways: the audit item is behaviour-identical and would
qualify as "truly obvious," but it is recorded here anyway per the owner's
conservative default of recording quick default-flips on a user-visible surface;
and the deeper Crop redesign remains **backlogged**, not folded into this record.

The implementing visibility rule is `CropPagesDialog::CropPagesDialog`
(`src/ui/CropPagesDialog.cpp`, `m_allPagesCheck->setVisible(m_multiPage)`). The
larger direct-manipulation crop rework (drag-on-page crop with live preview, plus
all-zero-margin feedback) stays tracked in the backlog item
`docs/backlog/2026-07-15-crop-pages-direct-manipulation.md`; this record covers
only the single-page checkbox-noise removal.

## Evidence required to reopen

A measured case where a single-page document genuinely needs an "apply to all
pages" choice (e.g. a crop that must be remembered and re-applied as pages are
later added), plus owner sign-off. The backlogged crop redesign is the venue for
any richer multi-page crop model.
