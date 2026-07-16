# 0006 — Search-result seeding: document-order (always match 1), or position-aware (first match at/after the current page)?

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15

## Context

When a user invokes Find in Trailer, the initially-selected match — the one the
"X of Y" counter lands on and the viewport highlights first — is decided at a
single seed site. Today that seed is **always document index 0**, with zero
awareness of where the user is reading. The question this record exists to
settle — and does **not** pre-decide — is whether the first-selected match
should stay pinned to document order (match 1 of the whole document) or become
**position-aware**: the first match at or after the current page, preserving
whole-document coverage.

This record ratifies and grounds the feeding backlog item
`docs/backlog/2026-07-13-search-current-page-seed.md`; it does not fork it. The
owner's example there: on a document where page 12 carries matches, a user
reading around pages 9–11 who opens Find should land on match 6/9 (page 12's),
not on match 1 of the whole document.

**What ships today (so this record isn't misread as describing the target):**
the always-index-0 seed. `src/document/PdfAdapter.cpp:796` sets
`m_currentResult = query.isEmpty() ? -1 : 0;` — the page-unaware seed. The
cached/synchronous branch pushes index 0 into the view
(`PdfAdapter.cpp:806-808`), and the async large-doc path
(`onSearchResultsPopulated`, `PdfAdapter.cpp:816-831`) pushes the still-0
`m_currentResult` at `:829`, also with no page awareness. The forward/back
navigation (`findNext`/`findPrevious`, `PdfAdapter.cpp:898-918`) and the counter
(`currentSearchMatchIndex`, `:937-946`) already advance correctly off
`m_currentResult` and need no change — only the *seed* is wrong. The idioms a
position-aware seed would reuse already exist: `pagesWithSearchMatches()` reads
each result's page via the search model (`PdfAdapter.cpp:949-960`),
`resultAtIndex(i).page()` is used at `:885-888`, and the viewport page is
`currentPage()` (`PdfAdapter.cpp:760-763`).

External grounding (research theme
`docs/research/2026-07-13-ux-research-agenda.md` → Theme 1). Two distinct
reference conventions bear on this:

- **Incremental find-next** (the most-ingrained baseline) advances from the
  user's *current position* and wraps around. macOS's standard Find bar is built
  on `NSTextFinder`, whose next-match action ("Find Next", ⌘G) advances from the
  current selection
  (https://developer.apple.com/documentation/appkit/nstextfinder,
  https://developer.apple.com/documentation/appkit/nstextfinder/action/nextmatch;
  ⌘G documented at https://support.apple.com/en-us/102650). Browser Ctrl+F is the
  same baseline: Firefox's Find starts at the user's "invisible cursor" / first
  visible element and only starts at the document beginning when nothing has been
  clicked or focused, and this position-aware behaviour was deliberately kept
  (resolved WONTFIX as intended)
  (https://bugzilla.mozilla.org/show_bug.cgi?id=346271). Adobe Acrobat's **Find
  toolbar** (⌘F) is a find-next-from-here tool with Next/Previous over the active
  document (https://helpx.adobe.com/acrobat/using/searching-pdfs.html).
- **Global full-text results list** keeps whole-document coverage and orders
  matches in **page order**. Acrobat's Advanced **Search** panel lists results
  "in page order, nested under the names of each searched document," with context
  and optional sort by Relevance / Date / Filename / Location
  (https://helpx.adobe.com/acrobat/using/searching-pdfs.html). Skim presents
  found text as a list in its Contents pane that you click to jump to
  (https://skim-app.sourceforge.io/manual/SkimHelp_7.html). Apple Preview
  highlights every match across the whole document and navigates via
  next/previous (https://osxdaily.com/2016/09/10/search-in-pdf-preview-mac/).
- **PDF Expert** is reported to start its results list at the current page rather
  than the document start — the direct behaviour the owner wants — but this could
  not be confirmed verbatim from Readdle's own documentation and is marked
  **(needs-live-verification)**
  (https://apphelp.readdle.com/pdfexpert6/index.php?pg=kb.page&id=1368,
  https://support.readdle.com/pdfexpert/en_US/reading-pdfs/use-smart-search).

The convention that emerges: *coverage is whole-document and ordered by page,
but the match you land on first is the one at/after where you are.* The owner's
expected behaviour is the synthesis of the two conventions above — panel-style
whole-document coverage with find-next-style position-aware seeding — not an
idiosyncratic choice. Where a specific app's initial-seed page could not be
confirmed from documentation, the claim is marked below.

## Options

- **A. Document-order seed (today).** The first-selected match is always
  document index 0. Whole-document coverage; the counter always opens at "1 of
  Y." Matches the shipped `PdfAdapter.cpp:796` behaviour. Simplest, fully
  deterministic, but ignores the reader's position — the exact
  dogfood complaint ("stuck on match 1 of whole doc").
- **B. First match at/after the current page, wrap to 0 (owner's expected).**
  Whole-document coverage is unchanged; only the seed moves. Seed to the
  smallest result index whose page ≥ `currentPage()`; if the viewport is past
  the last match, wrap the seed to index 0. `findNext`/`findPrevious` continue to
  advance/counter off `m_currentResult` unchanged. Mirrors the find-next
  convention (Firefox/`NSTextFinder`/Acrobat Find) at **page** granularity while
  keeping the page-ordered results list of the panel convention.
- **C. Selection/cursor-anchored seed (finer-grained variant of B).** Anchor the
  seed to the current text selection or caret position rather than page
  granularity — the literal `NSTextFinder`/browser "next from the invisible
  cursor" model — so the first match is the first one *after the selection*, not
  merely on-or-after the current page. Closest to the macOS/browser baseline, but
  Trailer's seed and counter are page-and-index based (`currentPage()`,
  `resultAtIndex(i).page()`); a caret anchor needs a text-position cursor the
  search seam does not currently carry, so it is strictly more than the backlog
  item asks and risks non-determinism in the UAT (no stable caret in a
  page-driven fixture).

## Personas debate

- **Office non-technical user:** Opens Find expecting it to help *here*, on the
  page in front of them. Under A, landing on match 1 of the whole document reads
  as "it jumped me somewhere else / it didn't find the one I'm looking at."
  Favours B: the first hit is the one near where they are reading. Has no stake
  in caret-vs-page granularity (Option C) as long as the first hit is nearby.
- **Older careful user:** Wants Find to be predictable and to not yank the
  viewport unexpectedly. B is *more* predictable for this lens than A, because
  the highlight stays near the current page instead of scrolling back to the top
  of the document. This lens' one requirement is that "next" then moves forward
  consistently from that seed and wraps once — which `findNext` already does
  unchanged. Would be uneasy with C only if a hidden caret made "where it starts"
  feel unexplainable.
- **Power migrator (ex-Preview/Acrobat/PDF Expert/browser):** Carries the
  find-next-from-here muscle memory from every one of those tools (⌘F/Ctrl+F
  advancing from the current position, wrapping around). Under A, Trailer is the
  odd one out that resets to the top. Strongly favours B; would accept C as
  "even more like the browser" but does not require sub-page precision to feel at
  home.
- **Occasional user:** Uses Find rarely; needs it to "just work" without a mental
  model. The concrete need is that the first highlight is visible/near the
  current view rather than off-screen at page 1. B serves this directly; A is the
  failure. Neutral on C.

The two contrasting lenses this record turns on:

- **Whole-document-coverage guardian ("don't let position-awareness hide
  matches"):** Fears that "seed at the current page" could be misread as "search
  only from the current page," dropping matches before it — the concrete failure
  would be "I searched and it missed the hit on page 3 while I was on page 10."
  Its stake is that coverage stays whole-document. Option B answers it directly:
  only the *seed index* moves; the populated result set and the counter's Y still
  span the whole document, and wrap-around plus `findPrevious` still reach the
  earlier pages. This lens rules out any implementation that filters the result
  set by page rather than merely choosing the seed within it.
- **Position-fidelity lens ("land me where I'm reading"):** Wants the first match
  to reflect the reading position, per the universal find-next convention. Its
  stake is that opening Find at page 12's neighbourhood seeds page 12's match,
  not page 1's. This is the dogfood report's own complaint and the reason Option
  A is inadmissible as a *target* (it is only the current state).

## Admissible objections

- **Office / occasional user, Option A:** opening Find while reading a middle
  page selects match 1 of the whole document and scrolls the viewport away from
  what they were reading; concrete failure at "I opened Find on page 12 and it
  threw me to page 1's match." This is the dogfood report verbatim and the
  decisive argument against keeping A as the target.
- **Whole-document-coverage guardian, a naive Option B:** if "seed at the current
  page" were implemented by *restricting* the search or the result list to pages
  ≥ current, matches before the current page vanish — concrete failure "Find
  missed an earlier hit." Option B is only admissible as **seed-only**: the full,
  page-ordered result set is preserved and only `m_currentResult`'s initial value
  changes; `findPrevious` and wrap-around still reach earlier pages. The backlog
  item's fix direction encodes exactly this (choose the seed index by walking the
  already-populated `m_searchModel`; do not change coverage).
- **Older careful / power migrator, Option C's hidden caret:** anchoring the seed
  to a text caret the viewer doesn't visibly carry makes "where Find starts"
  unexplainable and, in a page-driven UAT fixture, non-deterministic — the seed
  could depend on invisible selection state. Concrete failure: "I can't predict
  or test where the first match lands." This is why C is not the seed-behaviour
  this record would establish, even though it is the closest literal copy of the
  browser baseline.

### Rejected as naked preference

- "Search should always start at the top — that's how search works." — rejected:
  asserts a taste and is contradicted by the cited find-next convention
  (Firefox/`NSTextFinder`/Acrobat Find all advance from the current position); it
  names no user, step, or failure that position-aware seeding causes. The
  admissible concern in this neighbourhood (coverage must stay whole-document) is
  the guardian objection above, which Option B already answers.
- "It should feel exactly like Chrome, down to the caret." — rejected as a naked
  preference for Option C: states no user-step-failure that page-granular seeding
  (B) produces; the admissible version is the hidden-caret determinism objection,
  which points *away* from C for Trailer's page-based seam.

## Checkable threshold this record would establish

**If Option B is adopted (the owner's declared pass/fail, mirroring
`docs/backlog/2026-07-13-search-current-page-seed.md`), as accepted with the
revisions below:** the initially-seeded match is position-aware while coverage
stays whole-document, **and the seed lands correctly on the async large-doc
path, not merely the synchronous one.** Encoded as a deterministic UAT assertion
in a **new multi-page test** (extend `tests/uat/test_uat_search_and_markup.cpp`;
do **not** mutate the existing wrap test — see the keep-062 note):

1. **Seed at/after current page, first such.** Open a multi-page fixture whose
   query term appears on **distinct, known pages**; `goToPage(k)` for a middle
   page `k`; run the search. Assert `currentSearchMatchIndex()` maps to a match
   whose page is **≥ `currentPage()`** and is the **first** such match in
   document order — not page 0's match. (Owner's example: reading pages 9–11 on a
   doc with matches on page 12 seeds page 12's match, e.g. 6 of 9. Both compared
   page values are **0-based**, so the owner's "page 12" is page **index 11**.)
2. **On-current-page equality (`≥`, not `>`).** With the query term sitting **on
   the current page `k` itself** (`goToPage(k)` where page `k` carries a match),
   the seed must be **that page-`k` match** — pinning the comparison at `≥
   currentPage()`, not `> currentPage()`.
3. **Tie-break — first in reading order on the seed page.** The seed is the
   **smallest model index whose page ≥ `currentPage()`** — i.e. the first match
   in reading order on the at/after page. Where the seed page carries several
   matches, the earliest-indexed one wins.
4. **Wrap past the last match.** With the viewport positioned **past the last
   match's page**, the seed **wraps to index 0**.
5. **Streaming-order guard (async-populate; decisive, R1).** With a fixture
   whose model populates **incrementally in page order** so that an
   **earlier-page** match is inserted **before** the current-page match, the
   **final** seed after population settles must still be the **at/after-page**
   match — never a provisional earlier-page seed frozen by the "don't stomp user
   nav" guard. The seeding assertion must
   `QTRY_VERIFY_WITH_TIMEOUT` that the model is **fully populated**
   (`rowCount == knownTotal`) **before** reading `currentSearchMatchIndex()`,
   mirroring the existing wrap test's population wait — reading the seed before
   population settles is both flaky and can ship the R1 bug green.
6. **Coverage unchanged (guardian invariant).** The total match count `Y` still
   spans the whole document (matches before page `k` are still present and
   reachable): `findPrevious` from the seed reaches the earlier-page matches, and
   the counter's denominator equals the whole-document match total, not a
   page-filtered subset.

**Keep-062 (non-change, R6).** The existing wrap test's `QCOMPARE(startIdx, 0)`
(uat_vwr_062, `tests/uat/test_uat_search_and_markup.cpp` ~:516) **stays as-is**:
its fixture opens at page 0, where the position-aware seed is *legitimately* 0,
and it guards the page-0-open + `findNext`/`findPrevious` wrap path.
Position-aware seeding goes in the **separate new multi-page test above**, not by
mutating 062.

Pass = all of the above hold **including the async streaming-order guard** — the
seed must land correctly on the async path (R1's guard fix in place), not just
the synchronous one; fail = the seed lands on page 0's (or an earlier-page)
match from a middle page, or an on-current-page match is skipped by a `>` test,
or wrap does not occur past the last match, or a provisional earlier-page seed is
frozen on the async path, or coverage shrinks to pages ≥ current. **Option A**
establishes the opposite (seed is always index 0); **Option C** would
additionally require the seed to honour a caret/selection position, which the
current page-based seam and a page-driven fixture cannot assert deterministically
— so C does not get a stable threshold here.

## Arbiter verdict + rationale

**Accepted: Option B** (position-aware seed — first match at/after the current
page, wrap to index 0, whole-document coverage unchanged), **with revisions.**

Option B is the sole admissible *target*. Option A is not a candidate: it is only
the shipped state, and it is the dogfood failure itself ("I opened Find on page
12 and it threw me to page 1's match"). Option C does not survive its own
objection — a caret-anchored seed needs a text-position cursor the page-based
search seam does not carry, and it is non-deterministic in a page-driven UAT
(the seed would depend on invisible selection state), so it establishes no stable
threshold and cannot be the behaviour this record ratifies. With A inadmissible
as a target and C untestable on this seam, B is what remains — and it is exactly
the synthesis the cited find-next convention and the owner's backlog item ask
for. No admissible objection carries the decision to an alternative; the
whole-document-coverage guardian is fully answered by B's seed-only shape (only
`m_currentResult`'s initial value moves; coverage, `findPrevious`, and wrap still
span the whole document).

The proposed three-assertion threshold, however, is **green-but-hollow on the
async large-doc path** — the owner's own page-12 example runs through
`onSearchResultsPopulated`, not the synchronous branch — so acceptance is
conditioned on the following revisions, which only *add* precision and correct
the fix mechanism; no existing owner threshold is weakened.

- **R1 (decisive — async-populate guard).** `onSearchResultsPopulated` fires on
  every `rowsInserted`, and the model populates incrementally in page order.
  Computing `firstResultIndexAtOrAfter(currentPage())` against a
  **partially-populated** model can find nothing ≥ the current page yet, wrap to
  index 0, and push it; the existing "don't stomp user nav" guard at
  `PdfAdapter.cpp:825` (`if currentSearchResultIndex() >= 0 return`) then
  **freezes that provisional seed** on every later populate, parking the user on
  an earlier-page match. The raw `>= 0` test at `:825` **must not be relied on**.
  The implementation must distinguish a **provisional seed we pushed** from
  genuine user navigation: track the last provisionally-pushed seed index; on each
  populate, if the view's current index still equals our provisional push,
  **recompute** `firstResultIndexAtOrAfter(currentPage())` against the now-larger
  model and overwrite; only a value **differing** from our provisional push counts
  as user nav and freezes. (Equivalently: defer seeding until population settles.)
- **R2 (decisive — UAT population wait).** The seeding assertion must
  `QTRY_VERIFY_WITH_TIMEOUT` that the model is fully populated
  (`rowCount == knownTotal`) **before** reading `currentSearchMatchIndex()`,
  mirroring the existing wrap test's wait. Reading the seed before population
  settles is both flaky and can ship R1's bug green.
- **R3 (on-current-page equality).** The threshold pins `≥`, not `>`: an
  assertion where the query term sits **on** the current page `k` must seed
  **that** match. Both compared page values are 0-based (the owner's "page 12" is
  page index 11).
- **R4 (tie-break).** The seed is the smallest model index whose page ≥
  `currentPage()` — the first match in reading order on that page.
- **R5 (fixture prerequisite).** The current `writePdfWithKeywordTimes` helper
  stamps every copy on a **single** page and cannot express the threshold;
  acceptance requires a **new multi-page fixture helper** (query term on distinct
  known pages), plus the streaming-order regression from R1 (a current-page match
  arriving *after* an earlier-page match; the final seed is the at/after-page
  match).
- **R6 (non-change).** The existing wrap test's `QCOMPARE(startIdx, 0)`
  (uat_vwr_062, ~`:516`) **stays as-is** — its fixture opens at page 0 where the
  position-aware seed is legitimately 0, guarding the page-0-open +
  `findNext`/`findPrevious` wrap path. Position-aware seeding goes in the separate
  new multi-page test, not by mutating 062.

Pass now **requires the async guard fix (R1)**: the seed must land correctly on
the async path, not just the synchronous one.

## Evidence required to reopen

A reproducible case where a **correctly-seeded** position-aware first match
harms a real user at a real step — e.g. whole-document coverage demonstrably
shrinks under the seed-only implementation, or the R1 async-guard fix regresses
user-navigation preservation (a genuine user nav is overwritten as if it were a
provisional seed) — together with owner sign-off; or a superseding decision
record. A bare preference to "always start at the top" is not such evidence; it
is already rejected above as a naked preference.
