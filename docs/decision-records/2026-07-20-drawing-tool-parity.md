# 2026-07-20 — Drawing-tool parity: bounded shape tools match Ink (draw-first + sticky)

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the owner (programmerq), by his in-session one-word ruling "parity" (2026-07-20). The record documents and derives from that ruling; the owner remains the escalation-only override.
- **Date proposed:** 2026-07-20
- **Date accepted / superseded:** 2026-07-20 (accepted)
- **Builds on / extends:** the free-form Ink behaviour shipped in PR #91
  (draw-first-on-press, "Bug 3") and PR #94 / CF-3 (sticky-draw), plus backlog
  item `2026-07-20-freehand-auto-revert-drawover-noop`. Those two PRs left two
  one-line predicate seams scoped to Ink only, with an explicit OPEN PARITY
  QUESTION comment at each; this record closes that question.

## Context

Two interaction behaviours shipped for the free-form **Ink** (Freehand) tool but
were deliberately withheld from the **bounded shape tools** (Rectangle, Ellipse,
Line, Arrow) pending an owner call:

1. **Draw-first on press.** With Ink active, a press that lands on top of an
   existing annotation starts a NEW stroke — it does not select/move the shape
   underneath. Selecting an existing annotation is a Select-tool-only gesture
   (Preview/Acrobat convention). The bounded tools instead *hijacked* such a
   press into select-and-move (the pre-parity UAT-ANN-128).
2. **Sticky-draw.** After committing an Ink stroke the tool stays armed, so the
   user draws stroke after stroke. The bounded tools instead auto-reverted to
   Select on commit (the pre-parity UAT-ANN-131 / UAT-ANN-141-style behaviour),
   so a second draw-over drag silently became a rubber-band selection.

What shipped today (before this record): draw-first and sticky were `Ink`-only.
The two seams:

- Sticky: `isStickyDrawTool(AnnotationTool)` in
  [`src/ui/MainWindow.cpp:3704`](../../src/ui/MainWindow.cpp) (anonymous
  namespace above `onAnnotationCommitted`) — returned `tool == Ink`.
- Press-hijack: the select/move branch guard in
  `AnnotationOverlay::mousePressEvent`
  ([`src/ui/AnnotationOverlay.cpp:968`](../../src/ui/AnnotationOverlay.cpp)) —
  was `if (m_tool != AnnotationTool::Ink)`, so only Ink fell through to draw.

The open question (PHILOSOPHY → *Platform-native per OS* / Preview parity, and
the seam comments themselves): should the bounded shape tools also get both
behaviours? The owner answered in one word: **"parity"**.

## Options

- **A — Parity (accepted).** Give Rectangle / Ellipse / Line / Arrow BOTH
  behaviours, matching Ink. Selection becomes Select-tool-only for every drawing
  tool.
- **B — Draw-first only.** Make the bounded tools draw-first but keep them
  one-shot (revert to Select on commit).
- **C — Sticky only.** Make the bounded tools sticky but keep their
  select-on-press hijack.
- **D — Status quo.** Leave the bounded tools as-is (select-on-press,
  one-shot); Ink stays the lone exception.

## Personas debate

- **Power migrator (from Preview / Acrobat):** Expects a drawing tool to keep
  drawing and never to select the thing underneath — that is what every mark-up
  app does. Options C and D violate the first expectation; B and D violate the
  second. Only A matches muscle memory.
- **Office non-technical user:** Draws several boxes in a row to outline fields
  on a form. Under D/B they must re-click the toolbar between every box; under
  C/D a box drawn over an earlier one grabs the earlier one instead of drawing.
  A removes both surprises.
- **Older careful user:** Wants "the tool I picked does the one thing I picked
  until I change it" — predictability over cleverness. The old auto-revert (D)
  is the confusing case: the tool silently changed under them. A is the
  predictable rule.
- **Occasional user:** Rarely annotates; when they do, they expect clicking an
  existing shape to select it. Under A they must switch to the Select tool
  first. This is the one cost of A (see admissible objections) — mitigated by
  the Select tool being the obvious, labelled home for selection, and by the
  draw-first rule being consistent across *all* drawing tools rather than a
  per-tool coin-flip.

## Admissible objections

- **Occasional user, "I clicked my rectangle to select it and got a new one" —**
  under A, a press with a drawing tool active no longer selects. Concrete flow:
  Rectangle armed, user taps an existing rectangle expecting a selection ring,
  gets a (near-zero-size, dropped) new-shape gesture instead. Failure: the
  selection they wanted didn't happen. **Resolution:** this is the deliberate
  parity trade — selection is a Select-tool gesture, exactly as in Preview. A
  pure click (no drag) with a bounded tool creates nothing (the <1pt bounds are
  dropped), so the user is not left with litter; they switch to Select (one
  click) to select. Consistency across all drawing tools is worth more than the
  per-tool select-on-click shortcut.

### Rejected as naked preference

- "Reverting to Select after each shape feels tidier." — rejected: states no
  concrete user, step, or failure; and it is the exact behaviour CF-3 showed
  causes a silent draw-over no-op. Taste, outweighed by an admissible failure.

## Scope actually applied

Both behaviours apply to the tools the ruling names, plus Ink (already done):
**Rectangle, Ellipse, Line, Arrow, Ink.**

**Draw-first (behaviour 1)** is implemented as the cleanest form — the
select/move hit-test branch runs **only** for `m_tool == Select`, so *every*
non-Select tool draws-first. This is safe and correct beyond the named set:

- SAM tools (InstantAlpha / SmartLasso) never reach the branch — they have their
  own press path earlier in `mousePressEvent`.
- Text / Note / SpeechBubble still open their inline editor on release; they only
  lose the click-to-select-an-existing-annotation shortcut, which now belongs to
  Select alone — their edit-mode model is unchanged.
- Text-markup tools (Highlight / Underline / StrikeOut) still operate on the
  text-selection drag on release.
- The stamp/region tools (HighlightShape / Redaction / ZoomLens / Signature)
  draw-first, consistent with the parity rule.

**Sticky (behaviour 2)** is scoped **narrowly** to the named set + Ink. It is an
explicit allow-list in `isStickyDrawTool`. Deliberately **excluded** (stay
one-shot → revert to Select):

- Text / Note / SpeechBubble — never reach the commit path (they open an inline
  editor and `return` before emitting `annotationCommitted`), so sticky is moot.
- Highlight / Underline / StrikeOut — text-markup, single application.
- HighlightShape / Redaction / ZoomLens / Signature — single-placement
  stamp/region tools; reverting keeps the just-placed item grabbable.

None of the excluded tools were named in the "parity" ruling. HighlightShape,
Redaction and ZoomLens are geometric drag-to-draw tools and are the closest to
"ambiguous"; per the conservative default they are **kept one-shot** for sticky.
If the owner wants any of them to become sticky too, that is a one-word
follow-up — adding the enumerator to the `isStickyDrawTool` allow-list is the
entire change.

## Checkable threshold this record establishes (G1)

All observable pass/fail, provable headlessly:

1. **Draw-first, per bounded tool.** With Rectangle / Ellipse / Line / Arrow
   armed, a press-drag STARTING inside an existing shape's bounds creates a NEW
   shape of that type; the original is neither selected (`selectedAnnotationId
   == 0`) nor moved (bounds unchanged). (`test_freehand_selection_precedence::
   boundedToolPressStartsNewShape`; UAT `uat_ann_128_drawingToolPressStartsNewShape`,
   `uat_ann_133_boundedToolsDrawFirstOverExisting`.)
2. **Sticky, per bounded tool, via the toolbar.** With the tool armed via the
   markup toolbar, committing a shape leaves both the toolbar and overlay on
   that tool; a second drag draws a second shape. (UAT
   `uat_ann_131_boundedShapeStaysStickyAfterCommit`,
   `uat_ann_134_boundedToolsAreStickyViaToolbar`.)
3. **Select tool unaffected.** With Select active, a click still selects an
   existing annotation (UAT-ANN-120), and the resize-handle drag on a selected
   annotation still works (the handle hit-test is already gated to `m_tool ==
   Select`). (`selectToolStillSelectsOnClick`; UAT-ANN-124.)
4. **Ink unchanged.** Ink still draws-first and stays sticky (UAT-ANN-129,
   UAT-ANN-132).

## Consequences

- The pre-parity **UAT-ANN-128** ("drawing-tool click selects an existing
  annotation") and **UAT-ANN-131** ("toolbar auto-switches to Select after a
  one-shot shape") are **inverted** by this decision. Their spec text
  (`docs/uat/05-annotations.md`) and harness slots are updated in the
  implementing PR to assert the new behaviour; the `boundedToolStillSelectsOnClick`
  unit assertion added in #91 is likewise inverted to `boundedToolPressStartsNewShape`.
- No user-visible default outside the drawing tools changes.

## Magic constants (G6)

None. This record changes two boolean predicates (a tool-set membership test and
a branch guard), not a tuned numeric constant.

## Evidence

- Unit: `tests/test_freehand_selection_precedence.cpp`
  (`boundedToolPressStartsNewShape` covers all four bounded tools; the Ink and
  Select guards are unchanged).
- UAT (label `uat`): `tests/uat/test_uat_search_and_markup.cpp` —
  `uat_ann_128_drawingToolPressStartsNewShape`,
  `uat_ann_131_boundedShapeStaysStickyAfterCommit`,
  `uat_ann_133_boundedToolsDrawFirstOverExisting`,
  `uat_ann_134_boundedToolsAreStickyViaToolbar`, with the Ink guards
  `uat_ann_129` / `uat_ann_132` unchanged.
- G2 before/after evidence committed under `docs/uat/images/`.

## Evidence required to reopen

A concrete flow where draw-first or sticky for a bounded tool causes a user at a
named step to lose work or reach a state they never passed through (not "it feels
different"), plus owner sign-off; or an owner ruling extending/retracting the
tool set.
