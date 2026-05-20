#pragma once

#include "ml/MlScheduler.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QPolygon>
#include <QVector>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

namespace trailer {

class Application;
class IDocument;
class SamSession;

// Coordinates MobileSAM submissions for the active document of a
// single MainWindow. Owns a shared `SamSession` instance and an LRU
// cache of prepared encoder embeddings keyed by
// `(document-id, page-index, image-hash)` so flipping between recent
// pages does not re-encode.
//
// Submission shape (Workstream G):
//   - On tool activation (Instant Alpha / Smart Lasso) the controller
//     submits `SamSession::prepare(image)` to `Application::mlScheduler()`
//     at `UserAction` priority. The result populates the LRU cache.
//   - On every mouseMove during a SAM tool drag the overlay calls
//     `requestSegment(positives, negatives, cb)`. The controller
//     throttles to ~30 Hz (one decoder pass in flight at a time;
//     coalesce intermediate calls).
//   - On commit (mouseRelease for Instant Alpha; double-click / Enter
//     for Smart Lasso) the overlay reads the last cached mask /
//     polygon directly off the session (no fresh decoder run needed).
//
// PHILOSOPHY:
//   - No popups for failure. A null mask / empty polygon = no commit.
//   - No telemetry, no network outside the existing `ModelDownloader`.
//   - LRU cache stays bounded (kLruCapacity entries) so a long
//     scrolling session can't grow memory unboundedly.
//
// Lifetime:
//   - The controller is parented to its MainWindow and lives until
//     the window closes. Its destructor cancels every outstanding
//     scheduler task so worker threads can drain cleanly.
class SamController : public QObject {
    Q_OBJECT
  public:
    explicit SamController(Application *app, QObject *parent = nullptr);
    ~SamController() override;

    // Pixel-data hash used as a cache key for the encoder LRU.
    // Distinct from `SelectableTextStore::hashImageContent` despite
    // serving a similar purpose: the OCR store needs a hash that's
    // stable across QImage format conversions (it canonicalises to
    // Format_RGB888 first), while this hash is keyed by the exact
    // image we encoded — folding image.format() and walking the
    // raw buffer including row padding is fine for "is this the
    // same image we already encoded?" semantics and avoids the
    // format-conversion copy on every key compute.
    static std::uint64_t hashImageContent(const QImage &image);

    // Switch which document is "active" for the tool. Cancels any
    // in-flight prepare / segment for prior submissions. Does not
    // touch the LRU cache — switching back to a recent (doc, page)
    // re-uses the cached embedding without re-encoding.
    void setDocument(IDocument *doc, int page);
    IDocument *document() const { return m_doc; }
    int page() const { return m_page; }

    // Returns true iff the SAM models are on disk.
    bool isModelReady() const;

    // Submit an encoder prepare pass at `UserAction` priority for the
    // given image. The result is cached under (doc, page, hash) in the
    // LRU. `onPrepared` runs on the GUI thread when the encoder
    // returns; the bool is `true` on success, `false` on cancellation
    // or model error.
    //
    // If the (doc, page, hash) entry is already cached, `onPrepared`
    // is invoked synchronously with `true` and no scheduler task is
    // queued. Use `isCachedForActive(image)` to skip the call entirely
    // when the cache says we're already prepared for this image.
    //
    // Repeated calls cancel the previous prepare submission so the
    // worker thread is not chewing through stale encoder runs.
    using PreparedCallback = std::function<void(bool prepared)>;
    void prepareForActive(const QImage &image, PreparedCallback onPrepared);

    // Returns true iff there's already a cached encoder embedding for
    // the active (doc, page, hash(image)). Cheap (O(N) over the LRU
    // which is capped at kLruCapacity).
    bool isCachedForActive(const QImage &image) const;

    // Request a decoder pass. `positives` and `negatives` are in the
    // source image's pixel coordinates. `onResult` runs on the GUI
    // thread with the produced mask (null on failure / cancel).
    //
    // Throttle policy:
    //   - If a decoder is currently running, store the new prompts as
    //     "pending" — when the running decoder completes, the latest
    //     pending prompts run next. Earlier pending submissions are
    //     discarded (so a fast drag generates only the freshest
    //     decoder pass).
    //   - Otherwise submit immediately at `UserAction` priority. The
    //     scheduler runs it on the shared worker thread.
    //
    // The minimum interval between successful decoder dispatches is
    // `kThrottleIntervalMs`; quicker re-requests wait. The 33 ms
    // floor matches a ~30 Hz live preview budget — the user does
    // not perceive separate frames slower than that, and the decoder
    // itself is <10 ms on CPU per `SamSession.h`.
    using SegmentCallback = std::function<void(const QImage &mask)>;
    void requestSegment(QVector<QPoint> positives, QVector<QPoint> negatives,
                        SegmentCallback onResult);

    // Cancel any pending / in-flight prepare + segment for this
    // controller. The session itself stays alive; cached embeddings
    // remain.
    void cancelAll();

    // Drop every cached embedding belonging to `doc`. Wired from the
    // MainWindow's documentAboutToBeRemoved listener so a closed
    // document does not leave stale entries pointing at a recycled
    // allocator address.
    void purgeDocument(IDocument *doc);

    // Snapshot the last-produced mask. Empty when no segment has run
    // for the active session.
    QImage lastMask() const;

    // Polygon contour of the last mask, for Smart Lasso commit. Empty
    // when no segment has run.
    QPolygon lastContour() const;

    // The minimum interval between consecutive decoder dispatches.
    // Exposed for tests so the throttle interval is observable.
    static constexpr int kThrottleIntervalMs = 33;

    // Maximum number of cached encoder embeddings. Picked so two
    // adjacent pages + the page the user is on each get a slot
    // without the cache growing without bound on a long flip-flop.
    static constexpr int kLruCapacity = 3;

    // Test seam: drain any pending throttled-segment dispatch
    // synchronously. Returns true iff a dispatch fired. Used by the
    // throttle unit test to walk past the timer wait without burning
    // wall-clock seconds.
    bool flushPendingSegmentForTest();

    // Test seam: number of cached encoder embeddings. Tests use this
    // to verify LRU eviction.
    int cacheSizeForTest() const;

    // Test seam: number of decoder dispatches issued (running or
    // completed). Used by the throttle unit test to count distinct
    // segments produced from a burst of rapid mouseMoves.
    int decoderDispatchCountForTest() const;

    // Test seam: insert a synthetic cache entry under
    // (doc, page, hash). Bypasses the encoder so LRU tests don't need
    // real model weights. Returns the number of entries in the cache
    // after insertion.
    int insertSyntheticCacheEntryForTest(IDocument *doc, int page, std::uint64_t hash);

  private:
    // Cache entry. The embedding lives inside SamSession's own
    // m_embedding member; the controller's cache only tracks the key
    // and the size of the prepared image so we can restore the
    // session's state on a hit. `lastUsed` is a monotonic counter for
    // LRU eviction (we don't use std::chrono so tests are
    // deterministic).
    struct CacheKey {
        IDocument *doc = nullptr;
        int page = -1;
        std::uint64_t hash = 0;
    };
    struct CacheEntry {
        CacheKey key;
        std::vector<float> embedding;
        QSize origSize;
        float scale = 0.0f;
        std::uint64_t lastUsed = 0;
    };

    // Find the cache entry matching `key`, or nullptr.
    CacheEntry *findCache(const CacheKey &key);
    const CacheEntry *findCache(const CacheKey &key) const;
    // Insert or update; evicts LRU when over capacity. Bumps lastUsed
    // on the inserted entry.
    void putCache(CacheEntry entry);
    // Re-bind the SAM session to the entry — fills `m_embedding`,
    // `m_origSize`, `m_scale` so a subsequent segment() runs against
    // this embedding.
    void rebindSession(const CacheEntry &entry);

    // Run a decoder pass through the scheduler. The caller arranges
    // throttling around this; the function itself just submits.
    void dispatchDecoder(QVector<QPoint> positives, QVector<QPoint> negatives,
                         SegmentCallback onResult);

    Application *m_app;
    IDocument *m_doc = nullptr;
    int m_page = 0;

    // shared_ptr (not unique) so worker-thread lambdas can capture
    // a copy by value and keep the session alive past controller
    // destruction. cancelAll() flips active tokens but the encoder
    // pass may still be running when the controller frees; the
    // shared_ptr defers SamSession destruction until the worker
    // exits.
    std::shared_ptr<SamSession> m_session;

    // LRU cache. Linear scan over kLruCapacity entries is cheap.
    std::deque<CacheEntry> m_cache;
    std::uint64_t m_useCounter = 0;

    // The id of the active prepare submission, if any. Cancelled if a
    // new prepareForActive arrives.
    MlTaskId m_prepareTask = 0;

    // The id of the in-flight decoder submission, if any.
    MlTaskId m_decoderTask = 0;
    // True while a decoder is queued or running; suppress further
    // dispatch and stash the latest prompts in m_pending* below.
    bool m_decoderInFlight = false;
    // Latest pending prompts; when the in-flight decoder completes,
    // the controller flushes these. Re-assignment overwrites; rapid
    // mouseMoves coalesce to the freshest sample.
    QVector<QPoint> m_pendingPositives;
    QVector<QPoint> m_pendingNegatives;
    SegmentCallback m_pendingCallback;
    bool m_havePending = false;
    // Wall-clock timestamp (steady_clock) of the last dispatched
    // decoder. Used to space subsequent decoder runs at least
    // kThrottleIntervalMs apart so a frame-by-frame mouseMove burst
    // does not saturate the worker.
    std::chrono::steady_clock::time_point m_lastDispatch =
        std::chrono::steady_clock::time_point{};

    // Total number of decoder dispatches we've ever submitted. The
    // throttle test reads this to verify a burst of rapid mouseMoves
    // produces only a handful of dispatches.
    int m_decoderDispatches = 0;
};

} // namespace trailer
