---
id: 2026-08-28-tab-reorder-desyncs-document-index
title: DocumentView allows tab drag-reorder but m_documents never follows — index-based lookups target the wrong document afterward
priority: TBD
status: open
source: correctness-skeptic review pass, 2026-08-28 (pre-existing at base; surfaced while reviewing windowForOpenPath's tab-index use)
created: 2026-08-28
---

## Threshold

With two documents open in one window (legacy new_tab mode), drag tab 0
past tab 1, then: `DocumentView::currentDocument()` returns the document
whose tab is current, and `windowForOpenPath(pathOfSecondDoc, &idx)`
yields an idx whose `setCurrentIndex` selects that document's tab — both
asserted by a unit/UAT test that performs a programmatic `tabBar()->moveTab`.

## Context

`DocumentView` sets `setMovable(true)` (src/ui/DocumentView.cpp) but
nothing handles `QTabBar::tabMoved`, so `m_documents` keeps creation
order while tab indices reorder. Every index-based bridge —
`currentDocument()`, `documentAt()`, `windowForOpenPath`'s tabIndex,
`showDocumentAt` — silently targets the wrong document after a drag.
Fix shape: connect `tabBar()->tabMoved(from,to)` and permute
`m_documents` in lockstep (mirroring the QTabWidget page move), or key
lookups by widget instead of index.
