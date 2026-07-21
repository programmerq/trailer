#pragma once

#include "ml/OcrEngine.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace trailer {

// Bounded, content-hash-keyed, on-disk LRU cache of per-page OCR
// results (ADR 0013 §G13.4). This is the persistence tier that sits
// UNDER the per-document, in-memory `SelectableTextStore`: OCR survives
// application restart / document reopen without re-running the model,
// while the total on-disk footprint is capped so it can never grow
// unbounded.
//
// Keying — CONTENT HASH ALONE. Entries are keyed by the 64-bit source-
// image content hash (`hashImageContent` / `SelectableTextStore::
// contentHashFor`), NOT by (document, page). Two pages — in the same or
// different documents — that render to identical pixels therefore share
// a single cache entry (cross-document dedup is free and correct), and a
// page whose pixels change (edit, external file change) renders to a NEW
// hash and simply MISSES the cache. The content-hash key IS the
// invalidation seam: there is no separate "wipe on reload" step — a
// changed page can never be served stale because its hash no longer
// matches any entry (ADR 0013 §G13.4 (ii)/(iii)).
//
// Transparency. Nothing about this cache is ever user-visible — no
// setting, no status text, no menu (ADR 0013 §G13.4 (iv) / owner
// directive "the user shouldn't know if we cache").
//
// Threading. All access is expected on the GUI thread (the OcrController
// reads through on submit and writes through in the store's GUI-thread
// apply step), so the cache carries no internal locking. Do not call it
// from a worker thread.
class OcrDiskCache {
  public:
    // PHILOSOPHY: hand-tuned values stay hand-tuned. Total on-disk size
    // ceiling for the OCR result cache. 256 MB is the value ratified in
    // ADR 0013 §G13.4 (it mirrors the 256 MB thumbnail-cache budget
    // landed in PR #55 for one memory story). Range/symptom to change:
    // too small and a large multi-page scan thrashes (constant re-OCR of
    // pages that were evicted between visits); too large and the cache
    // wastes disk. Serialized OCR blocks are small (KB per page), so
    // 256 MB holds a very large working set of recognized pages. Change
    // this only with the measured reopen evidence ADR 0013 names
    // ("Evidence required to reopen" (d)) — a usage pattern where the
    // ceiling demonstrably thrashes or is wastefully large.
    static constexpr std::int64_t kOcrDiskCacheCeilingBytes =
        static_cast<std::int64_t>(256) * 1024 * 1024;

    // `cacheDir` defaults (via the no-arg constructor below) to
    // AppPaths::ocrCacheDir(); tests pass a temporary directory so they
    // never touch the real data dir. `ceilingBytes` is exposed only so
    // tests can drive eviction with small, fast fixtures — production
    // always uses kOcrDiskCacheCeilingBytes.
    OcrDiskCache(QString cacheDir, std::int64_t ceilingBytes = kOcrDiskCacheCeilingBytes);

    // Convenience production constructor: cacheDir = AppPaths::ocrCacheDir().
    OcrDiskCache();

    // Look up the cached blocks for `contentHash`. Returns nullopt on a
    // miss, or when the on-disk file is corrupt / a format-version or
    // hash mismatch (the bad file is dropped so it can't poison future
    // reads). On a hit the entry is touched (moved to most-recently-used)
    // so the LRU ordering reflects access.
    std::optional<std::vector<OcrEngine::TextBlock>> load(std::uint64_t contentHash);

    // Write-through: persist `blocks` for `contentHash` (atomic temp +
    // rename), mark it most-recently-used, then evict least-recently-used
    // entries until the total on-disk size is within the ceiling. Empty
    // `blocks` are not written (a text-less page has nothing to cache and
    // must not create an entry that would later be served as a "hit").
    void store(std::uint64_t contentHash, const std::vector<OcrEngine::TextBlock> &blocks);

    // Drop the entry for `contentHash` if present. Idempotent. Used by a
    // forced re-run so a stale entry can't survive a re-OCR that yields
    // fewer/garbage blocks.
    void remove(std::uint64_t contentHash);

    // True iff an entry for `contentHash` currently exists.
    bool contains(std::uint64_t contentHash) const;

    // Total bytes currently accounted for across all entries. Kept in
    // sync incrementally; used by the ceiling test and by eviction.
    std::int64_t totalBytes() const { return m_totalBytes; }

    // Number of entries currently held. Exposed for tests.
    std::size_t entryCount() const { return m_index.size(); }

    std::int64_t ceilingBytes() const { return m_ceilingBytes; }

  private:
    struct Entry {
        std::int64_t size = 0;  // serialized file size in bytes
        std::uint64_t seq = 0;  // access sequence: larger == more recent
    };

    QString pathFor(std::uint64_t contentHash) const;
    // Rebuild the in-memory index from whatever is already on disk,
    // ordering entries by file mtime (oldest → smallest seq) so LRU
    // ordering is reasonable across an application restart.
    void reindexFromDisk();
    void enforceCeiling();

    QString m_dir;
    std::int64_t m_ceilingBytes;
    std::int64_t m_totalBytes = 0;
    std::uint64_t m_seqCounter = 0;
    std::unordered_map<std::uint64_t, Entry> m_index;
};

} // namespace trailer
