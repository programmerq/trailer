# G10 Audit — Deference and Spatial Constancy — 2026-07-31

> Single-lens audit against gate **G10** ([`AGENTS.md`](AGENTS.md) §*Hard
> gates*, detail in [`docs/ux-guidelines.md`](ux-guidelines.md)), run at the
> owner's request immediately after G10 merged (#132, `9a4a101`). Follows the
> structure of [`docs/audit-2026-05-19.md`](audit-2026-05-19.md): numbered,
> stable-ID findings; confirmed-good positives called out alongside the
> problems; a closing action register. Findings are ID'd `DEF-<severity>-N`
> (deference) and `SC-<severity>-N` (spatial constancy) rather than
> per-lens letters, since this pass has exactly one lens with two named
> principles.

> **2026-08-01 process note.** This document originally shipped as its own
> docs-only PR (#134), which filed three backlog items for SC-CRIT-1,
> SC-CRIT-2, and DEF-MOD-1 (see the *Backlog items filed* section below).
> The owner correctly flagged that shape as too small to be its own PR —
> it doesn't deliver anything — and, checking the files, it was worse
> than a shape preference: those three backlog items would have landed
> already-obsolete, since all three findings are fixed elsewhere. #134 is
> being folded into the PR that fixes SC-CRIT-1/SC-CRIT-2/SC-MOD-1
> (originally #141, `fix: eliminate status-bar, markup-toolbar, and
> search-bar reflow`, stacked onto this branch so the audit and its fixes
> land as one deliverable), which also fixes DEF-MOD-1 in a sibling PR
> (#140, `UI deference polish`). All three backlog items are deleted in
> this same PR, closing them the way `docs/backlog/README.md` requires
> (deletion in git, citing the item id) — see each finding's own
> **Fixed** note below and the updated **Action register** for exactly
> which PR closed which. The findings themselves, the confirmed-good
> positives, and the G10-wording assessment are otherwise unchanged from
> the original audit — they remain the record of what was found and why
> it matters, independent of how the fixes shipped.

**Scope.** All of `src/ui/` read against G10's two questions — *does this
add permanent chrome that duplicates what the document already shows?* and
*does an existing control move on screen as a side effect of unrelated
state?* — plus a pass over dialogs/modals against PHILOSOPHY's "no popup
that just says no" and `docs/ux-guidelines.md`'s subtle-affordances rule,
per the coordinator's brief. Read-only: no `src/` files were changed to
produce this audit.

**Method.** Full read of `MainWindow.cpp`/`.h`, `Sidebar.cpp`,
`DocumentView.cpp`, `AnnotationOverlay.cpp`, `Inspector.cpp`,
`MarkupToolbar.cpp`, `FormToolbar.cpp`, `SearchBar.cpp`,
`FileChangeBanner.cpp`, `AnimationBar.cpp`, `EmptyStateWidget.cpp`,
`FeedbackDialog.cpp`, `MlProgressWidget.cpp`, `ModelManagerDialog.cpp`,
`PreferencesDialog.cpp`; targeted greps across the rest of `src/ui/` for
`addPermanentWidget`, `setVisible`, `QMessageBox`, menu-rebuild patterns,
and widget-title construction. Six sibling agents are working `src/ui/`
concurrently in the shared checkout; every finding below was checked
against `git branch -a` in the shared repo plus a diff of each named
in-flight branch against `9a4a101` before being written up, specifically
to avoid re-filing work already moving.

**Known in-flight — confirmed present, not re-filed.** Each of these was
independently located in the current tree and matches the coordinator's
description; they are listed here only so the audit's coverage is
traceable, not as new findings:

- Permanent zoom-% readout — `MainWindow.cpp:604-610` (construction),
  `:3229-3239` (`updateZoomIndicator()`). → `claude/image-open-zoom-window-size`.
- Toolbar-row reflow / main toolbar not anchored — largely already fixed on
  `main` by decision record `0007-toolbar-anchoring-and-overflow` (see
  `MainWindow.cpp:1000-1033` for the anchoring comment trail); further work
  in progress on → `claude/toolbar-reserved-positions` (one commit ahead,
  reasserting layout after a stale persisted `windowState` blob resurrects
  the pre-fix order).
- View-mode menu items reordering by active mode — `MainWindow.cpp` view
  menu / `syncViewModeActions()` (~`:1355-1391`, `:4450`). →
  `claude/mode-switch-and-search-nav`.
- "Recovery Snapshot Saved" (`MainWindow.cpp:843`), sidebar labelled
  "Sidebar" (`Sidebar.cpp:255`, `MainWindow.cpp:1038,1042`), PDF dark-mode
  background (`PdfAdapter.cpp:1371-1373`). → `claude/ui-deference-polish`.
- Missing I-beam cursor / no live text-selection feedback. →
  `claude/pdf-text-selection`. (Not independently re-verified in depth —
  trusted from the brief; it's a selection-affordance bug, not directly a
  G10 chrome/position question, so it wasn't a focus of this pass.)

---

## Findings — Spatial constancy

### SC-CRIT-1: The status bar's permanent-widget row is a reflow chain — six independently-toggling widgets, each shift every widget after it

**What the user sees.** The status bar's right-hand ("permanent widget")
region hosts, in this left-to-right order:

1. `m_mlIndicator` — "ML" chip, visible only while `MlScheduler` has a
   non-idle task running (`MainWindow.cpp:516-520`, shown/hidden at `:527-528`).
2. `m_largeDocOcrHint` — the "Recognize text on this page" link chip,
   visible only for large un-OCR'd documents, dismissable (`:545-601`,
   shown/hidden at `:594`, `:599`, `:4235`).
3. `m_zoomIndicator` — the permanent zoom readout named above (in-flight
   fix elsewhere; still present today at `:604-610`).
4. `m_readOnlyBadge` — the Two-Pages "Read-only" pill, visible only in
   Two-Pages view mode (`:621-629`, shown/hidden at `:4085-4086`,
   `:4133-4134`).
5. `m_mlProgress` — the OCR/ML progress+cancel widget, visible only while
   a foreground ML batch is running (`:635-636`; internal show/hide in
   `MlProgressWidget.cpp:44,68,83,110,126`).
6. `m_ocrModelMissingHint` — the "install language pack" link chip,
   visible only when auto-OCR wants to run but the model is missing
   (`:716-743`, shown/hidden at `:745`).

`QStatusBar::addPermanentWidget()` packs its widgets into one
left-to-right box layout in call order; a widget's `hide()` removes it
from that layout's effective width, so **every widget positioned after
it slides left, and slides back right when it reappears.** All six of the
above toggle independently of one another, on independent triggers (ML
scheduler activity, OCR-hint dismissal, document zoom, view-mode
switching, ML batch progress, and auto-OCR model availability) — so at
least four of the six (1, 2, 4 driving the position of 5 and 6) can move
a widget the user is not looking at, for a reason the user did not
initiate.

The concrete, checkable case: switch the current document into **Two
Pages** view mode (a view-mode change — nothing to do with ML) while an
OCR batch is running. `m_readOnlyBadge` appears, and `m_mlProgress` (the
progress bar + cancel button the user may be mid-interaction with) jumps
right by the badge's width. Symmetrically, dismissing the large-doc OCR
hint (`largeDocOcrHint`, item 2) shifts `m_readOnlyBadge`, `m_mlProgress`,
and `m_ocrModelMissingHint` all one slot left.

**Principle violated.** Spatial constancy — `m_mlProgress`'s cancel
button (a control the user may be about to click) moves as a side effect
of the *view mode*, which is unrelated state.

**Where.** `src/ui/MainWindow.cpp:516-743` (the six `addPermanentWidget`
calls and their independent visibility drivers, cited above per-widget).

**Severity/priority.** Critical for the effect the owner described
("moves its furniture") — this is the single most systemic instance of
it in the surface: not one control moving once, but a *chain* where six
controls can perturb each other in any combination. Distinct from the
in-flight zoom-readout fix: that fix removes item 3 from the
always-a-widget-in-the-chain set (a deference fix — the readout itself
shouldn't be permanent), but does not by itself resolve the
*positional coupling* between the remaining five items 1-2, 4-6 — the
chain-reflow problem persists even after the zoom readout becomes
transient, unless items are given reserved slots the way ADR 0007
reserved the toolbar rows.

**Suggested direction.** The toolbar fix (decision record
`0007-toolbar-anchoring-and-overflow`) already established the pattern
Trailer wants for this exact class of bug: reserve position rather than
let visibility toggles reflow neighbours. Apply the same idea to the
status bar — either (a) give each permanent widget a fixed-width slot
that reserves its footprint even when hidden (Qt: keep the widget
`show()`n with empty/transparent content, or wrap each in a container
with `setSizePolicy` fixed and manually blank it), or (b) group the
widgets that are allowed to co-occur into a single container widget
added once, so hide/show happens inside a sub-layout that doesn't
perturb the other status-bar residents. A geometry-assertion UAT slot
(same shape as ADR 0007's `uat_xct_076`) pinning "every permanent
widget's rect is unaffected by any other permanent widget's visibility
toggle" would be the checkable G1 threshold and the regression guard.

**Fixed (2026-08-01, this PR).** Option (a) shipped: each of the five
permanent widgets is now wrapped in a `reserveStatusBarSlot()` container
(`src/ui/MainWindow.cpp`) sized to the widget's own measured `sizeHint()`
at runtime; `setVisible()` blanks the slot's content instead of
collapsing it, so no widget's position depends on any other's
visibility. Regression guard: `uat_xct_078_statusBarPermanentWidgetsNeverReflowEachOther`
(`tests/uat/test_uat_ml_affordances.cpp`) — exactly the geometry-assertion
UAT slot suggested above, verified to fail against the pre-fix code.
Backlog item `2026-07-31-status-bar-permanent-widget-reflow-chain`
closed (deleted) in this PR.

---

### SC-CRIT-2: MarkupToolbar's tool buttons shift position on every document/tab switch

**What the user sees.** `MarkupToolbar::setToolVisible()`
(`src/ui/MarkupToolbar.cpp:214-253`) hides individual `QAction`s inside
the *same* toolbar row — not a whole sibling toolbar — based on document
capability:

- `Underline` / `Highlight` / `StrikeOut` are shown only when the current
  document `hasTextLayer()` (`MainWindow.cpp:4019-4022`).
- `Instant Alpha` / `Smart Lasso` are shown only for eligible image
  documents whose SAM models are available or download-permitted
  (`MainWindow.cpp:4029-4042`).

Both call sites run from `onCurrentDocumentChanged()`, i.e. on every
document/tab switch. Because a `QToolBar`'s actions live in one
left-to-right layout, hiding an action collapses its slot and shifts
every subsequent action left — the `Redact` button, the SAM-tools
separator, `Instant Alpha`/`Smart Lasso` themselves, and the trailing
`Stroke` / `Fill` / `Width` / `Dash` controls all move. Switching from an
OCR'd PDF tab to a scanned-image tab with no text layer (a routine
multi-tab move for the reference user handling "the PDF a notary just
emailed back" alongside a scanned insurance photo) visibly shifts every
markup control to the tool button's left — including the colour swatches
and width spinner the user's eye and mouse were tracking.

**Principle violated.** Spatial constancy — the tool buttons the user
reaches for by muscle memory (Redact, Stroke, Fill) move as a side
effect of which *document* is active, which is unrelated to those
controls' own function.

**Where.** `src/ui/MarkupToolbar.cpp:214-253` (`setToolVisible`);
call sites `src/ui/MainWindow.cpp:4019-4022`, `:4029-4042`.

**Severity/priority.** Critical, same class as SC-CRIT-1: a control the
user directly manipulates (not read-only, unlike the status bar chips)
moving under their cursor. Not covered by `claude/toolbar-reserved-positions`
— that branch's own UAT additions (`docs/uat/06-cross-cutting.md`
UAT-XCT-074/075/076/077, verified against its diff) are scoped to
*toolbar-level* row/position (`insertToolBar`/`insertToolBarBreak`
ordering, and the *main* toolbar's own action geometry against a sibling
toolbar's visibility) — not to individual `QAction`s appearing/
disappearing *inside* the markup toolbar itself. Distinct bug, same
family.

**Suggested direction.** Either (a) keep the action always present and
switch to `setEnabled(false)` + tooltip (G3-consistent: "Highlight is
available once this page has recognisable text") instead of
`setVisible(false)`, which trades the reflow for a small always-there
row of dimmed icons — a real trade-off worth a design call, since the
codebase elsewhere deliberately prefers hiding a genuinely inapplicable
tool over greying it (see `MainWindow.cpp:4026-4028`'s own comment:
"PHILOSOPHY: a tool the user cannot act on is hidden, not greyed"); or
(b) reserve each hideable action's slot the ADR-0007 way so hiding
blanks the icon without collapsing the layout. (a) is the smaller diff
but is itself a G3/G10 trade-off the arbiter should make explicitly,
which is why this is flagged rather than fixed here.

**Fixed (2026-08-01, this PR).** Option (a) shipped, as the trade-off this
finding flagged: `MarkupToolbar::setToolVisible()` became `setToolEnabled()`
— the five tool actions stay always-visible and disable-with-tooltip
instead of hiding, reversing the "hidden, not greyed" default named
above specifically for this shared, position-sensitive row (decision
record `docs/decision-records/2026-08-01-markup-toolbar-disable-not-hide.md`,
status `accepted`, since this is a recorded-default reversal per G6).
Regression guards: `tests/test_markup_toolbar.cpp`'s
`toolPositionsNeverMoveAcrossEnableDisableToggles` (isolated-widget level)
and `uat_xct_079_markupToolbarActionsStayPutAcrossDocumentTypeSwitch`
(`tests/uat/test_uat_search_and_markup.cpp`, integrated MainWindow level)
— both verified to fail against the pre-fix code. Backlog item
`2026-07-31-markup-toolbar-tool-visibility-reflow` closed (deleted) in
this PR.

---

### SC-MOD-1: SearchBar's match counter shifts the prev/next/close buttons — flagged, not filed (overlap risk)

**What the user sees.** `SearchBar` (`src/ui/SearchBar.cpp:12-53`) lays
out `[input, stretch=1][counter][prev][next][close]`. `m_counter` starts
`hide()`-den and only `show()`s once a query has ≥1 match
(`setMatchCounter()`, `:55-67`). Because `m_input` is the only stretching
widget, `m_counter`'s hide/show does not resize the input — but the
`prev`/`next`/`close` buttons sit *after* the counter, so the moment a
query goes from 0 matches to ≥1 (or the field is cleared back to empty),
the "previous match" / "next match" / "close" buttons shift right or
left by the counter's ~60px minimum width. A user typing a search term
letter-by-letter can watch Next/Previous slide sideways as the match
count crosses zero.

**Principle violated.** Spatial constancy — the navigation buttons move
as a side effect of the match *count* reaching zero, not as a side
effect of anything the user did to those buttons.

**Where.** `src/ui/SearchBar.cpp:24-28` (layout order), `:55-67`
(`setMatchCounter`).

**Not filed as a backlog item — at the time.** `claude/mode-switch-and-search-nav`'s
branch name pairs "mode-switch" with "search-nav"; the coordinator's
known-in-flight list names only the view-mode-menu-reordering half of
that branch, so it's genuinely unclear whether "search-nav" already
covers this. Filing a backlog item risked a silent duplicate per
`docs/backlog/README.md`'s own stated failure mode ("slightly-different
slugs for the same work"), so this was recorded here only, with the
recommendation that whoever picks up `claude/mode-switch-and-search-nav`
check this file:line before starting new work, and that it be filed
separately only if that branch's scope turns out not to include it.

**Fixed (2026-08-01, this PR).** The overlap risk above was checked
directly rather than left open: `claude/mode-switch-and-search-nav` (PR
#139, merged into `main` before this PR branched) was read diff-by-diff —
its `SearchBar.cpp`/`.h` changes add **only** a Shift+Enter `eventFilter()`
for Find Previous, zero lines touching `setMatchCounter()` or the layout
order. No overlap, so this was fixed here rather than filed as a separate
item. `SearchBar`'s counter never `hide()`s now — it stays visible at a
fixed width (derived from the running platform's own live font metrics,
not a literal — see the CI-fix history on the implementing PR for why
that distinction mattered), blank when there is nothing to report, so
Prev/Next/Close never move as the match count crosses zero. Primary
regression guard: `tests/test_search_bar.cpp`'s
`navButtonsStayPutAsMatchCountCrossesZero` (a bare, unconstrained
`SearchBar` instance — the `MainWindow`-embedded one carries a
`setMaximumWidth(360)` that happens to absorb the defect at the app's
default window size and so does not independently catch it); secondary
integrated coverage: `uat_xct_080_searchBarNavButtonsStayPutAsMatchCountCrossesZero`.

---

## Findings — Deference

### DEF-MOD-1: Inspector dock title is "Inspector" — same defect class as the in-flight Sidebar fix, different widget

**What the user sees.** `Inspector::Inspector(QWidget *parent) :
QDockWidget(tr("Inspector"), parent)` (`src/ui/Inspector.cpp:82`). Opened
on demand (hidden at construction, `MainWindow.cpp:372`; shown when the
user selects an annotation, `:377-380`), the dock's title bar reads
"Inspector" — the chrome naming itself rather than describing what it
currently shows (a selected annotation's page/type/stroke/fill/etc.),
exactly the pattern already identified for the Sidebar
(`docs/ux-guidelines.md`'s own example: *"A sidebar labelled 'Sidebar.'
A label that describes the chrome instead of what it contains is the
chrome announcing itself."*).

**Principle violated.** Deference — chrome narrating itself instead of
deferring to its content.

**Where.** `src/ui/Inspector.cpp:82`.

**Severity/priority.** Moderate. Same fix shape as the Sidebar title
(likely a one-line change: retitle to describe the current selection, or
drop the redundant title text the dock frame already visually separates
from the document). Distinct widget from the one named in the
known-in-flight list (`Sidebar.cpp:255`), so there is a real risk the
`claude/ui-deference-polish` PR patches only `Sidebar` and this sibling
instance survives untouched. Filed as a backlog item (below) precisely
because it is not named in the brief and is easy to miss if the in-flight
PR's diff is scoped narrowly to the reported instance.

**Fixed (2026-08-01, PR #140, `UI deference polish`) — not by this PR.**
The named risk above materialised exactly as predicted (a concurrent
agent implementing the Sidebar fix on a scope limited to the reported
instance), except it was caught rather than missed: #140's own PR body
records finding this audit's DEF-MOD-1 finding mid-implementation and
sweeping Inspector into the same fix rather than leaving the sibling
instance behind. Both docks now use a shared `buildTextlessDockTitleBar()`
helper (`src/ui/IconHelper.h`/`.cpp`) — `windowTitle()` stays "Inspector"
(unchanged, since `QDockWidget`'s built-in accessibility interface reads
it directly rather than `accessibleName()` — verified empirically, per
that PR's own writeup) but the *painted* title bar is now a textless
strip with the same close/float affordances a stock title bar would
show. Pinned by `uat_fnd_019_dockPanelsHaveNoVisibleCaptionButKeepAccessibleName`.
Backlog item `2026-07-31-inspector-dock-title-names-itself` closed
(deleted) in this PR, crediting #140 as the fix, since stacking this
audit doc onto the PR that fixes SC-CRIT-1/SC-CRIT-2 does not itself
touch `Inspector.cpp`.

---

## Confirmed-good positives

Worth recording since G10 landed recently and it would be easy to read
this audit as "nothing works" — several places already show the pattern
the gate wants, in some cases with real engineering behind them:

- **ADR `0007-toolbar-anchoring-and-overflow` is a model of doing this
  right.** `MainWindow.cpp:1000-1033` and `:1143-1179` show real,
  deliberate work: `insertToolBar`/`insertToolBarBreak` ordering pins the
  main toolbar to row 1 regardless of markup/form visibility; the
  overflow chevron is pinned to a fixed width via class-targeted
  stylesheet so its own neighbours don't reflow when it toggles; and the
  main toolbar's minimum width is *pre-computed* against the search bar's
  opened footprint (`:1162-1178`) specifically so opening Find never
  pushes the row into its own "show more" overflow. This is exactly the
  "reserve position, don't let visibility toggle it" discipline SC-CRIT-1
  and SC-CRIT-2 above are asking for elsewhere.
- **`m_mlIndicator` / `m_largeDocOcrHint` / `m_ocrModelMissingHint`
  individually are correct "quiet ambient indicator" instances** —
  each hidden by default, each surfaces only when its condition is true,
  each links straight into the sanctioned one-time-consent download flow
  rather than popping a dialog (`MainWindow.cpp:516-745`). The *content*
  of each widget is exactly what `docs/ux-guidelines.md` asks for; only
  their *shared row* has the SC-CRIT-1 coupling problem above.
- **`FileChangeBanner` is a correct dialog-avoidance case, not a
  narration banner.** It only appears for a genuine decision (external
  file conflict / deletion), states each button's consequence in its
  label ("Reload (discard my edits)"), and its not-yet-built `Compare`
  button is disabled with a full G3 tooltip rather than silently doing
  something else (`src/ui/FileChangeBanner.cpp:56-64`).
- **`EmptyStateWidget`'s Recent list is restrained** — a plain list of
  names with the path as a hover tooltip, no chrome beyond what
  `docs/ux-guidelines.md` asks for (`src/ui/EmptyStateWidget.cpp:76-155`).
- **The genuinely-irreversible-action popups all check out**: redaction
  first-use warning, Reset Trailer confirmation, and the unsaved-changes
  prompt on close (`MainWindow.cpp:4637-4679`, `:4892-4906`,
  `:5125-5148`) are all decisions the user would not want made
  implicitly — correct PHILOSOPHY use, not "no" popups.
- **`AnnotationOverlay` carries zero permanent chrome** — all
  annotation feedback (selection handles, drag state) paints on the page
  itself, the deference-correct default.

---

## Backlog items filed (historical — all closed as of this PR)

At the time this audit first shipped as PR #134, three items were filed,
each genuinely independent, checkable, and not already in-flight under
the names given:

- `docs/backlog/2026-07-31-status-bar-permanent-widget-reflow-chain.md`
  — SC-CRIT-1.
- `docs/backlog/2026-07-31-markup-toolbar-tool-visibility-reflow.md`
  — SC-CRIT-2.
- `docs/backlog/2026-07-31-inspector-dock-title-names-itself.md`
  — DEF-MOD-1.

**All three are deleted in this PR** — closed the way
`docs/backlog/README.md` requires (deletion in git, not a status edit),
because all three findings are now fixed: SC-CRIT-1 and SC-CRIT-2 by this
PR (stacked with the audit doc itself), DEF-MOD-1 by #140. See each
finding's own **Fixed** note above for the specifics; `git log` over each
deleted path recovers this closure and, further back, the original filed
item's full text. (No links to the files remain here, since they no
longer exist — see the *Action register* below for the current,
non-stale status of every finding.)

**Deliberately not filed, then or since:**

- SC-MOD-1 (SearchBar counter shift) — originally recorded rather than
  filed, due to a real naming-overlap risk with
  `claude/mode-switch-and-search-nav`'s "search-nav" half. That risk was
  checked directly and fixed rather than filed once resolved — see
  SC-MOD-1's own **Fixed** note above.
- The five known-in-flight items — already tracked on their named
  branches; filing a backlog item would either duplicate that work or,
  worse, get picked up by an eighth agent racing the branch already
  moving.
- Every item under *Confirmed-good positives* — nothing to track, no
  action needed.

Three findings total beyond the known-in-flight set; all three are now
fixed, none remain open. This audit intentionally did not produce a long
tail of minor findings — see *Prioritisation* below.

---

## Evaluating G10 itself

The brief asked for this explicitly, and applying the gate for a full
read of `src/ui/` surfaced real texture worth reporting back.

**Where G10 held up well.** The two-question test in the gate's own
*Test* clause — "what did this add to the permanent surface and why must
it be permanent" / "does anything move as a side effect of unrelated
state" — was easy to apply mechanically once a widget's construction
site and its visibility-driving signal were both in hand. Every finding
above was locatable by asking exactly those two questions of exactly
one `addPermanentWidget`/`addAction`/`setVisible` call at a time. The
gate's **Evidence** clause (prefer a geometry assertion over a
screenshot) is well-calibrated: SC-CRIT-1 and SC-CRIT-2 are both stated
above as things a `QWidget::geometry()`/`pos()` comparison would catch
outright, which is exactly the kind of regression guard `ADR 0007`'s own
UAT additions already demonstrate work well in this codebase.

**Where it was genuinely hard to apply — three real ambiguities.**

1. **"Permanent" doesn't mean what it says when the state it reports is
   itself intermittent.** `m_readOnlyBadge` is described in its own code
   comment as "the always-visible PRIMARY read-only signal" — but it is
   only visible *while in Two-Pages mode*, which is itself an
   intermittent state. Read one way, this is not "permanent chrome" at
   all (it appears/disappears with the exact state it reports, which is
   the *opposite* of the zoom-readout problem — the zoom readout was
   permanent regardless of whether zoom-related information was
   relevant). Read the other way, "permanent" in the gate could mean
   "not on-demand / not summoned by a deliberate user action" — under
   that reading the badge fails, because the user didn't ask for a
   status readout, they asked to switch view modes and got one as a side
   effect. **The gate's wording doesn't disambiguate these two readings
   of "permanent,"** and they produce opposite verdicts for a real,
   ADR-accepted control (`docs/decision-records/2026-07-21-two-page-layout.md`,
   ruling D2-A). **Suggested fix:** add a sentence distinguishing
   *state-coupled* chrome (visible if and only if the state it reports is
   true — the read-only badge, the OCR-missing-model hint) from
   *session-coupled* chrome (visible for the whole session/document once
   shown — the old zoom readout). Only the second is what "permanent"
   in the gate's Rule is trying to name; the first is closer to a
   correctly-implemented on-demand affordance and should read as passing,
   not as an open question every reviewer re-litigates.

2. **Deference and "the document already shows it" have a genuine
   grey zone for state the document does *not* visually encode.**
   Two-Pages mode's read-only-ness is real information the rendered page
   spread does not communicate on its own (nothing about the pixels
   changes to say "you can't edit this"). Under the gate's literal Rule
   ("chrome that reports state the user can already perceive on the
   document itself"), the badge is *not* a violation — the state isn't
   perceivable from the document. That's the correct outcome here (this
   audit does not flag the badge's *existence*, only, hypothetically, its
   position in the reflow chain), but it took deliberate reasoning to
   reach, and a less careful pass could easily over-apply the gate and
   flag every status-bar item as "redundant chrome" without checking
   whether the state is actually document-visible. **Suggested fix:**
   the gate's Test clause could name this directly — "chrome is exempt
   from question 1 if the state it reports is not otherwise visible on
   the rendered document" — to head off both false positives (flagging
   the badge) and false negatives (an agent assuming any status-bar
   widget is fine because *something* is ADR-blessed nearby).

3. **A panel opening is desirable motion that isn't the target of the
   Rule, but the Rule's literal words don't carve it out.** Opening the
   Inspector dock (on annotation selection) or the Sidebar (on user
   toggle) visibly resizes the central document area — a "position
   change" of the document viewport itself, which is about as central a
   control as exists. Read hyper-literally, G10's Rule ("does not change
   the on-screen position of an existing control as a side effect of
   unrelated state changing") could be misapplied to forbid *any* dock
   widget from ever opening, since the central widget's position
   changes and the trigger (a docked panel opening) is not itself the
   control that moved. The gate clearly does not intend this — the
   **Boundary with G3** clause and the worked examples in
   `docs/ux-guidelines.md` (toolbar reflow, menu reordering) are all
   about *incidental, uncaused-by-the-user-in-that-control* movement, not
   about a control's own, requested, direct-manipulation resize. This
   audit did not flag dock-widget open/close, `DocumentView`'s
   `setTabBarAutoHide()`-driven tab strip appearing on a second tab, or
   any other *directly-requested* layout change as a violation, but
   spent real time confirming each one was in this category rather than
   the SC-CRIT-1/2 category before excluding it. **Suggested fix:** add
   one clause to the gate: "excludes layout changes that are the direct,
   visible consequence of the user's own action in that same
   interaction (opening a panel, adding a tab) — the target is motion
   the user did not cause and would not expect."

**Net assessment.** G10 is a well-specified, checkable gate for the
common case — the worked examples in `docs/ux-guidelines.md` (zoom
readout, autosave toast, sidebar label, toolbar reflow, menu reorder) map
cleanly onto real bugs this audit found more instances of (SC-CRIT-1,
SC-CRIT-2). The three ambiguities above are all boundary cases the gate's
authors likely had in mind but didn't write down; none of them suggest
the gate is wrong, only that a future reviewer will re-derive the same
reasoning from scratch each time without the wording fix. Recommend
folding the three suggested sentences into `docs/ux-guidelines.md`'s
existing *"Quick self-check before adding UI"* list (items 5-6) rather
than into G10 itself, since AGENTS.md documents that ux-guidelines.md is
"the detail behind the gate, not a second, softer standard" — the gate's
one-paragraph Rule should stay short; the disambiguation belongs where
the worked examples already live.

---

## Action register

| Finding | Action | Status |
|---|---|---|
| SC-CRIT-1 | Fixed — `reserveStatusBarSlot()`, this PR. Backlog item `2026-07-31-status-bar-permanent-widget-reflow-chain` closed (deleted) in this PR. | **Closed** |
| SC-CRIT-2 | Fixed — `MarkupToolbar::setToolEnabled()` + decision record `2026-08-01-markup-toolbar-disable-not-hide`, this PR. Backlog item `2026-07-31-markup-toolbar-tool-visibility-reflow` closed (deleted) in this PR. | **Closed** |
| SC-MOD-1 | Fixed — `SearchBar`'s counter stays visible at a fixed width, this PR. Checked `claude/mode-switch-and-search-nav` (#139)'s diff directly: no overlap, so fixed here rather than filed. Never had a backlog item to close. | **Closed** |
| DEF-MOD-1 | Fixed — textless dock title bar (`buildTextlessDockTitleBar()`), **PR #140**, not this PR. Backlog item `2026-07-31-inspector-dock-title-names-itself` closed (deleted) in this PR, crediting #140 as the fix. | **Closed** |
| G10 wording ambiguities (3) | Proposed as `docs/ux-guidelines.md` self-check additions above | Awaiting owner/arbiter |
| 5 known-in-flight items | Confirmed present, left to their named branches | In progress elsewhere |
