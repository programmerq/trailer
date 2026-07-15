---
id: 2026-07-15-bounded-ondisk-ocr-cache
title: Bounded on-disk OCR cache (content-hash keyed, 256 MB LRU, invalidated on edit, transparent, never unbounded)
priority: P2
status: open
source: ADR 0012 (OCR pipeline for images) — G12.4, deferred (disk-cache implementation)
created: 2026-07-15
---

## Threshold

Per **ADR 0012 §G12.4**. Give `SelectableTextStore` an on-disk backing cache so
OCR survives reopen, bounded so it never grows without limit. Declared pass/fail
(unit tests over the cache):

1. **Content-hash keyed.** Entries are keyed by the source-image content hash
   (`hashImageContent` / `SelectableTextStore::contentHashFor`), so a page whose
   pixels match a cached entry restores its text **without re-OCR**.
2. **Size-capped LRU.** Inserting entries past a **256 MB** total-size ceiling
   evicts least-recently-used entries so on-disk size stays ≤ the ceiling —
   **never unbounded** (matches the 256 MB thumbnail-cache budget landed in
   PR #55, for one memory story).
3. **Invalidated on edit.** Editing a page's pixels drops that page's cached
   entry (wired through the existing `SelectableTextStore::invalidate(page)`,
   `src/document/SelectableTextStore.h:65`).
4. **Transparent.** No user-facing control or status ever exposes the cache
   ("the user shouldn't know if we cache").

## Context

`SelectableTextStore` is **in-memory only** today — its header says so and marks
the disk cache a follow-up: *"In-memory only — no disk persistence in this phase.
Re-OCRing on reopen is acceptable; a disk cache is a follow-up."*
(`src/document/SelectableTextStore.h:18-20`). Invalidation is already by content
hash: callers stash the source hash alongside results (`hashImageContent`,
`SelectableTextStore.h:97`; `contentHashFor`, `:75`), and per-page pixel edits
call `invalidate(page)` (`:65`) — so the keying and invalidation seams the disk
cache needs already exist; this item adds the bounded on-disk tier behind them.

ADR 0012 accepts the **design** (content-hash key, 256 MB LRU ceiling,
invalidate-on-edit, invisible to the user); this item is the deferred
**implementation**, and §G12.4 is its acceptance test. Grounded by
`docs/decision-records/0012-image-ocr-pipeline-lazy-window-bounded-cache.md`
(§G12.4). The 256 MB ceiling is a hand-tuned value ratified there; changing it
needs the reopen evidence the ADR names (a usage pattern where the ceiling
thrashes or is wastefully large).
