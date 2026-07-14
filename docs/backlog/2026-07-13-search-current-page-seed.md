---
id: 2026-07-13-search-current-page-seed
title: Cmd+F seeds the first match at document start, ignoring the current page ("stuck on match 1 of whole doc")
priority: P2
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

Search still covers the whole document, but the initially-selected match is the
first one **at or after the current page**, not always document index 0.

Declared pass/fail (deterministic UAT, extend
`tests/uat/test_uat_search_and_markup.cpp`):

Open a multi-page fixture with the query term on several pages,
`goToPage(k)` for a middle page k, run the search, and assert
`currentSearchMatchIndex()` maps to a match whose page **≥ k** and is the
*first* such match — not page 0's match. Second assert: with the viewport past
the last match, seeding wraps to index 0.

Verified: from a middle page, the first highlight and the "X of Y" counter land
on the nearest match at/after the current page; Enter advances forward from
there; past the last match, it wraps.

## Context

Owner example: on a document where page 12 has matches, opening Find should
select match 6/9 (the one on page 12) when reading around page 9–11 — i.e. scan
forward to the current page's match — instead of parking on match 1 of the whole
document.

Root cause: the first result is always seeded to document index 0 with zero
current-page awareness.
- `src/document/PdfAdapter.cpp:796` — `m_currentResult = query.isEmpty() ? -1 :
  0;` (the bug seed).
- `src/document/PdfAdapter.cpp:806-808` — cached/synchronous branch pushes
  index 0 into the view.
- `src/document/PdfAdapter.cpp:816-831` — `onSearchResultsPopulated` (async
  large-doc path) pushes `m_currentResult` (still 0) at line 829; also no page
  awareness.
- `findNext`/`findPrevious` (`PdfAdapter.cpp:898-918`) and the counter
  (`currentSearchMatchIndex`, `:937-946`) already advance correctly off
  `m_currentResult` — they need **no** change.

The result-to-page idiom already exists:
`pagesWithSearchMatches()` reads `m_searchModel->index(i,0).data(
QPdfSearchModel::Role::Page)` (`PdfAdapter.cpp:949-960`), and
`resultAtIndex(i).page()` is used at `:885-888`. The viewport page to consult is
`currentPage()` (`PdfAdapter.cpp:760-763`).

Fix direction: add `firstResultIndexAtOrAfter(int page)` that walks the
populated `m_searchModel` (reusing the `pagesWithSearchMatches` idiom) and
returns the smallest index i whose page ≥ `currentPage()`, wrapping to 0.
Hook it at both seed sites (`:796`/`:806-808` synchronous, and the first
populate in `onSearchResultsPopulated` `:819-829`, keeping the "don't stomp user
nav" guard at `:825`). The search-navigation research theme in
`docs/research/2026-07-13-ux-research-agenda.md` feeds the position-aware-seeding
convention decision.

Cross-link: `2026-07-13-startup-hang-large-pdf` — same synchronous,
page-0-anchored `PdfAdapter.cpp` open/search seam.

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`.
