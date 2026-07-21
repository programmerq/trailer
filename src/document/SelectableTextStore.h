#pragma once

#include "ml/OcrEngine.h"

#include <QObject>

#include <cstdint>
#include <unordered_map>
#include <vector>

class QImage;

namespace trailer {

// Per-document, per-page cache of OCR results. Holds a vector of
// `OcrEngine::TextBlock` keyed by page index so multi-page PDFs round-
// trip cleanly (single-page documents only ever populate page 0).
//
// This store is the per-document, in-memory tier. Persistence across
// reopen is provided UNDER it by a bounded, content-hash-keyed on-disk
// LRU cache (`OcrDiskCache`, ADR 0013 §G13.4) owned by the
// `OcrController`: on a read miss the controller reads through the disk
// tier before running OCR, and on a successful pass it writes through.
// The store itself stays a pure in-memory container with no filesystem
// dependency — the disk tier is keyed by the same content hash the store
// stashes here (`contentHashFor`), so the two agree by construction.
//
// Invalidation is by content hash: callers stash the source-image hash
// alongside the results, and a page-level edit (rotate/crop/replace)
// signals invalidation through `invalidate(page)`. A future call to
// `put()` with a different hash for the same page is allowed and just
// replaces the entry.
//
// The store is a QObject so the UI overlay can listen for `changed()`
// after an OCR submission completes. Emissions are queued onto the GUI
// thread because OCR runs on the MlScheduler's worker thread; callers
// should `QObject::moveToThread` the store onto the UI thread before
// connecting to `changed()` (it is constructed on the UI thread by the
// document adapters, so no manual move is needed in practice).
class SelectableTextStore : public QObject {
    Q_OBJECT
  public:
    struct PageEntry {
        std::uint64_t contentHash = 0;
        std::vector<OcrEngine::TextBlock> blocks;
    };

    explicit SelectableTextStore(QObject *parent = nullptr);

    // True iff `put()` has been called for `pageIndex` and no
    // subsequent `invalidate()` has cleared it.
    bool hasResults(int pageIndex) const;

    // Returns the OCR blocks for `pageIndex`. Empty when no results
    // exist for that page — callers should pre-check with
    // `hasResults()` if they need to disambiguate "we OCR'd and got
    // zero blocks" from "we never OCR'd".
    const std::vector<OcrEngine::TextBlock> &blocks(int pageIndex) const;

    // Stash the result of a successful OCR pass. `contentHash` is the
    // hash of the source image at the time of OCR (used by callers to
    // detect a stale cache after a page-level edit). Overwrites any
    // existing entry for the page. Emits `pageChanged(pageIndex)` and
    // `changed()`.
    void put(int pageIndex, std::uint64_t contentHash,
             std::vector<OcrEngine::TextBlock> blocks);

    // Drop the entry for `pageIndex`. Idempotent — invalidating a
    // page that was never populated is a no-op. Emits `pageChanged`
    // and `changed()` only when an entry was actually removed.
    void invalidate(int pageIndex);

    // Drop every cached entry. Used on document close / replace.
    // Emits `changed()` if anything was actually cleared.
    void clear();

    // Read-only view of the recorded hash for a page. Returns 0 when
    // there is no entry. Used by the auto-OCR scheduler to decide
    // whether the current rendered page is still represented in the
    // store.
    std::uint64_t contentHashFor(int pageIndex) const;

    // "Attempted-and-empty" memo (ADR G13.1/G13.2 honesty seam). A page
    // that OCR'd to zero usable blocks deliberately does NOT get a
    // put() — hasResults() must stay false so the completion status is an
    // honest "No text found". But without a separate memo the ambient
    // cache-skip key (hasResults) would never fire for that page, so
    // onVisiblePageChanged would re-render + re-OCR it on every visit.
    // markAttempted records the page + the content hash it was OCR'd at;
    // wasAttempted returns true only while the recorded hash still
    // matches the current one, so an edited page (new hash) re-OCRs.
    // This memo is independent of hasResults(): it never makes a page
    // report selectable text.
    void markAttempted(int pageIndex, std::uint64_t contentHash);
    bool wasAttempted(int pageIndex, std::uint64_t contentHash) const;

  signals:
    // Emitted whenever the entry for `pageIndex` is added, replaced,
    // or removed. The overlay listens for this and refreshes its
    // hit-test cache for the affected page.
    void pageChanged(int pageIndex);

    // Fires after any change. Wired to the SelectableTextLayer's
    // generic repaint so the layer doesn't need a per-page slot.
    void changed();

  private:
    std::unordered_map<int, PageEntry> m_entries;
    // Attempted-and-empty memo: page index → content hash the page was
    // last OCR'd at when it yielded no usable blocks. Kept separate from
    // m_entries so it can never leak into hasResults()/blocks(). Dropped
    // for a page by invalidate(), and wholesale by clear().
    std::unordered_map<int, std::uint64_t> m_attempted;
    static const std::vector<OcrEngine::TextBlock> kEmpty;
};

// Compute a stable 64-bit hash of a QImage's pixel data. Cheap-ish
// (linear in pixel count, no allocations) and good enough to detect a
// rotated / cropped / replaced page. Not cryptographic — this is purely
// for cache invalidation. Returns 0 for null images so callers can use
// it as a "nothing to hash" sentinel.
std::uint64_t hashImageContent(const QImage &image);

} // namespace trailer
