#include "SamController.h"

#include "app/Application.h"
#include "ml/CancellationToken.h"
#include "ml/MlScheduler.h"
#include "ml/ModelRegistry.h"
#include "ml/SamSession.h"

#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace trailer {

namespace {

std::uint64_t mixHash(std::uint64_t a, std::uint64_t b) {
    // FNV-1a-ish 64-bit mixer. We don't need cryptographic strength;
    // we just want a stable scalar from (raw bytes, w, h, format).
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return a;
}

} // namespace

std::uint64_t SamController::hashImageContent(const QImage &image) {
    if (image.isNull())
        return 0;
    std::uint64_t h = 0x100000001b3ULL;
    h = mixHash(h, static_cast<std::uint64_t>(image.width()));
    h = mixHash(h, static_cast<std::uint64_t>(image.height()));
    h = mixHash(h, static_cast<std::uint64_t>(image.format()));
    // Hash the scanlines. QImage::sizeInBytes() is the buffer
    // including any per-row padding — fine for cache keying since we
    // only need to detect "this is a different image" with
    // overwhelming probability.
    const auto bytes = image.sizeInBytes();
    if (bytes <= 0)
        return h;
    const uchar *base = image.constBits();
    // Walk 8 bytes at a time when aligned; tail handled at the end.
    std::uint64_t bytesLeft = static_cast<std::uint64_t>(bytes);
    const std::uint64_t step = sizeof(std::uint64_t);
    while (bytesLeft >= step) {
        std::uint64_t chunk = 0;
        std::memcpy(&chunk, base, step);
        h = mixHash(h, chunk);
        base += step;
        bytesLeft -= step;
    }
    std::uint64_t tail = 0;
    if (bytesLeft > 0) {
        std::memcpy(&tail, base, bytesLeft);
        h = mixHash(h, tail);
    }
    return h;
}

SamController::SamController(Application *app, QObject *parent)
    : QObject(parent), m_app(app),
      m_session(std::make_shared<SamSession>(app ? &app->modelRegistry() : nullptr)) {}

SamController::~SamController() {
    cancelAll();
}

bool SamController::isModelReady() const {
    return m_session && m_session->isModelReady();
}

void SamController::setDocument(IDocument *doc, int page) {
    if (m_doc == doc && m_page == page)
        return;
    // Cancel in-flight work — anything that was about to land on the
    // previous (doc, page) is now stale.
    cancelAll();
    m_doc = doc;
    m_page = page;
}

void SamController::purgeDocument(IDocument *doc) {
    if (!doc)
        return;
    auto it = std::remove_if(m_cache.begin(), m_cache.end(),
                             [doc](const CacheEntry &e) { return e.key.doc == doc; });
    m_cache.erase(it, m_cache.end());
    if (m_doc == doc) {
        cancelAll();
        m_doc = nullptr;
        m_page = 0;
    }
}

SamController::CacheEntry *SamController::findCache(const CacheKey &key) {
    for (auto &e : m_cache) {
        if (e.key.doc == key.doc && e.key.page == key.page && e.key.hash == key.hash)
            return &e;
    }
    return nullptr;
}

const SamController::CacheEntry *SamController::findCache(const CacheKey &key) const {
    for (const auto &e : m_cache) {
        if (e.key.doc == key.doc && e.key.page == key.page && e.key.hash == key.hash)
            return &e;
    }
    return nullptr;
}

void SamController::putCache(CacheEntry entry) {
    // Drop any existing entry with the same key.
    auto it = std::remove_if(m_cache.begin(), m_cache.end(), [&entry](const CacheEntry &e) {
        return e.key.doc == entry.key.doc && e.key.page == entry.key.page &&
               e.key.hash == entry.key.hash;
    });
    m_cache.erase(it, m_cache.end());
    entry.lastUsed = ++m_useCounter;
    m_cache.push_back(std::move(entry));
    // Evict LRU until we are within capacity.
    while (static_cast<int>(m_cache.size()) > kLruCapacity) {
        // Find the entry with the smallest lastUsed.
        auto victim = m_cache.begin();
        for (auto i = m_cache.begin(); i != m_cache.end(); ++i) {
            if (i->lastUsed < victim->lastUsed) {
                victim = i;
            }
        }
        m_cache.erase(victim);
    }
}

void SamController::rebindSession(const CacheEntry &entry) {
    if (!m_session)
        return;
    // Re-install the cached embedding so a subsequent segment() runs
    // against the right prepared state without re-encoding. The
    // session also clears its m_lastMask — we'd otherwise hand back
    // stale prompts' output.
    m_session->setCachedState(entry.embedding, entry.origSize, entry.scale);
}

bool SamController::isCachedForActive(const QImage &image) const {
    if (!m_doc)
        return false;
    const std::uint64_t h = hashImageContent(image);
    const CacheKey key{m_doc, m_page, h};
    return findCache(key) != nullptr;
}

int SamController::cacheSizeForTest() const {
    return static_cast<int>(m_cache.size());
}

int SamController::decoderDispatchCountForTest() const {
    return m_decoderDispatches;
}

int SamController::insertSyntheticCacheEntryForTest(IDocument *doc, int page,
                                                    std::uint64_t hash) {
    CacheEntry e;
    e.key = {doc, page, hash};
    // A non-empty embedding so the LRU eviction logic treats this as
    // a real entry; the contents are irrelevant to the cache logic.
    e.embedding.assign(4, 0.0f);
    e.origSize = QSize(64, 64);
    e.scale = 1.0f;
    putCache(std::move(e));
    return static_cast<int>(m_cache.size());
}

void SamController::prepareForActive(const QImage &image, PreparedCallback onPrepared) {
    if (!m_app || !m_session) {
        if (onPrepared)
            onPrepared(false);
        return;
    }
    if (image.isNull() || !m_doc) {
        if (onPrepared)
            onPrepared(false);
        return;
    }

    const std::uint64_t h = hashImageContent(image);
    const CacheKey key{m_doc, m_page, h};

    if (auto *hit = findCache(key)) {
        // Cache hit — bump LRU and restore session state synchronously
        // so the next segment() call uses the right embedding.
        hit->lastUsed = ++m_useCounter;
        rebindSession(*hit);
        if (onPrepared)
            onPrepared(true);
        return;
    }

    if (!m_session->isModelReady()) {
        if (onPrepared)
            onPrepared(false);
        return;
    }

    // Cancel any previously-pending prepare.
    if (m_prepareTask != 0) {
        m_app->mlScheduler().cancel(m_prepareTask);
        m_prepareTask = 0;
    }

    // The worker carries a weak QPointer and hands the result back
    // THROUGH THE SCHEDULER, never through the controller itself: a
    // queued invoke dereferences its context object when it posts, so
    // posting to a controller the user destroyed mid-encode (window
    // closed during Instant Alpha / Smart Lasso) touches freed memory.
    // The scheduler joins this worker in its destructor, so it is
    // always a live context; the guard is re-checked on the GUI thread
    // inside the lambda. See MlScheduler::postResultToGuiThread().
    QPointer<SamController> selfGuard(this);
    MlScheduler *scheduler = &m_app->mlScheduler();
    // Capture the session by shared_ptr — the encoder lambda may
    // outlive the controller; the shared_ptr keeps SamSession alive
    // until the lambda exits (cancellation flips the token, the
    // encoder bails at its next checkpoint, and the shared_ptr
    // drops on lambda return).
    std::shared_ptr<SamSession> sess = m_session;
    auto handle = scheduler->submit(
        MlPriority::UserAction, tr("Preparing Instant Alpha / Smart Lasso…"),
        [selfGuard, scheduler, sess, image, key,
         onPrepared](CancellationToken &token) {
            if (token.isCancelled())
                return;
            // The encoder reads through the session's internal cache;
            // when it succeeds, the session exposes the embedding via
            // the friend accessor below.
            const bool ok = sess && sess->prepare(image, &token);
            if (token.isCancelled())
                return;
            std::vector<float> embedding;
            QSize origSize;
            float scale = 0.0f;
            if (ok && sess) {
                sess->cachedState(embedding, origSize, scale);
            }
            // The guard is checked on the GUI thread, inside the lambda:
            // that is the only thread that can destroy the controller, so
            // the check and the use below cannot interleave with it.
            scheduler->postResultToGuiThread(
                [selfGuard, ok, key, embedding = std::move(embedding), origSize, scale,
                 onPrepared]() {
                    if (!selfGuard)
                        return;
                    selfGuard->m_prepareTask = 0;
                    if (ok && !embedding.empty()) {
                        CacheEntry e;
                        e.key = key;
                        e.embedding = std::move(embedding);
                        e.origSize = origSize;
                        e.scale = scale;
                        selfGuard->putCache(std::move(e));
                    }
                    if (onPrepared)
                        onPrepared(ok);
                });
        });
    m_prepareTask = handle.id;
}

void SamController::requestSegment(QVector<QPoint> positives, QVector<QPoint> negatives,
                                   SegmentCallback onResult) {
    // Stash the latest prompts; if there's already a decoder in
    // flight, we just overwrite — the freshest sample wins.
    m_pendingPositives = std::move(positives);
    m_pendingNegatives = std::move(negatives);
    m_pendingCallback = std::move(onResult);
    m_havePending = true;

    // Throttle: if a decoder is already in flight, the on-finish
    // path will pick up the pending prompts. Otherwise check the
    // since-last-dispatch interval; dispatch immediately when we're
    // past the throttle floor, schedule a wake-up otherwise.
    if (m_decoderInFlight)
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - m_lastDispatch)
                             .count();
    if (m_lastDispatch == std::chrono::steady_clock::time_point{} ||
        elapsed >= kThrottleIntervalMs) {
        // Drain pending now.
        QVector<QPoint> p = std::move(m_pendingPositives);
        QVector<QPoint> n = std::move(m_pendingNegatives);
        SegmentCallback cb = std::move(m_pendingCallback);
        m_pendingPositives.clear();
        m_pendingNegatives.clear();
        m_pendingCallback = nullptr;
        m_havePending = false;
        dispatchDecoder(std::move(p), std::move(n), std::move(cb));
    } else {
        // Schedule a wake-up at the throttle boundary; the wake-up
        // re-checks m_havePending and dispatches the freshest prompts.
        // QTimer::singleShot with `this` as the context object only
        // fires when the receiver is still alive — the inner `controller`
        // pointer is safe to deref.
        const int waitMs = static_cast<int>(kThrottleIntervalMs - elapsed);
        SamController *controller = this;
        QTimer::singleShot(std::max(1, waitMs), this, [controller]() {
            if (!controller->m_havePending || controller->m_decoderInFlight)
                return;
            QVector<QPoint> p = std::move(controller->m_pendingPositives);
            QVector<QPoint> n = std::move(controller->m_pendingNegatives);
            SegmentCallback cb = std::move(controller->m_pendingCallback);
            controller->m_pendingPositives.clear();
            controller->m_pendingNegatives.clear();
            controller->m_pendingCallback = nullptr;
            controller->m_havePending = false;
            controller->dispatchDecoder(std::move(p), std::move(n), std::move(cb));
        });
    }
}

void SamController::dispatchDecoder(QVector<QPoint> positives, QVector<QPoint> negatives,
                                    SegmentCallback onResult) {
    if (!m_app || !m_session) {
        if (onResult)
            onResult(QImage());
        return;
    }
    ++m_decoderDispatches;
    m_decoderInFlight = true;
    m_lastDispatch = std::chrono::steady_clock::now();

    // Same weak-guard + scheduler-context hop as prepareForImage() above.
    QPointer<SamController> selfGuard(this);
    MlScheduler *scheduler = &m_app->mlScheduler();
    // Capture the session by shared_ptr so the decoder lambda owns
    // a strong reference for the duration of the inference, even if
    // the controller is destroyed mid-flight.
    std::shared_ptr<SamSession> sess = m_session;
    auto handle = scheduler->submit(
        MlPriority::UserAction, tr("Segmenting…"),
        [selfGuard, scheduler, sess, positives = std::move(positives),
         negatives = std::move(negatives), onResult](CancellationToken &token) {
            QImage mask;
            if (sess && !token.isCancelled()) {
                mask = sess->segment(positives, negatives, &token);
            }
            scheduler->postResultToGuiThread([selfGuard, mask, onResult]() {
                if (!selfGuard)
                    return;
                selfGuard->m_decoderTask = 0;
                selfGuard->m_decoderInFlight = false;
                if (onResult)
                    onResult(mask);
                // If a fresher set of prompts queued up while the
                // decoder was running, dispatch them now.
                if (selfGuard->m_havePending) {
                    QVector<QPoint> p = std::move(selfGuard->m_pendingPositives);
                    QVector<QPoint> n = std::move(selfGuard->m_pendingNegatives);
                    SegmentCallback cb = std::move(selfGuard->m_pendingCallback);
                    selfGuard->m_pendingPositives.clear();
                    selfGuard->m_pendingNegatives.clear();
                    selfGuard->m_pendingCallback = nullptr;
                    selfGuard->m_havePending = false;
                    selfGuard->dispatchDecoder(std::move(p), std::move(n), std::move(cb));
                }
            });
        });
    m_decoderTask = handle.id;
}

void SamController::cancelAll() {
    if (m_app) {
        if (m_prepareTask != 0)
            m_app->mlScheduler().cancel(m_prepareTask);
        if (m_decoderTask != 0)
            m_app->mlScheduler().cancel(m_decoderTask);
    }
    m_prepareTask = 0;
    m_decoderTask = 0;
    m_decoderInFlight = false;
    m_havePending = false;
    m_pendingPositives.clear();
    m_pendingNegatives.clear();
    m_pendingCallback = nullptr;
}

QImage SamController::lastMask() const {
    return m_session ? m_session->lastMask() : QImage();
}

QPolygon SamController::lastContour() const {
    return m_session ? m_session->contourFromLastMask() : QPolygon();
}

bool SamController::flushPendingSegmentForTest() {
    if (!m_havePending || m_decoderInFlight)
        return false;
    QVector<QPoint> p = std::move(m_pendingPositives);
    QVector<QPoint> n = std::move(m_pendingNegatives);
    SegmentCallback cb = std::move(m_pendingCallback);
    m_pendingPositives.clear();
    m_pendingNegatives.clear();
    m_pendingCallback = nullptr;
    m_havePending = false;
    dispatchDecoder(std::move(p), std::move(n), std::move(cb));
    return true;
}

} // namespace trailer
