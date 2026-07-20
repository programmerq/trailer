// Unit tests for OcrDiskCache — the bounded, content-hash-keyed on-disk
// LRU tier that persists OCR results under SelectableTextStore
// (ADR 0013 §G13.4). These are pure-container tests over a temporary
// cache directory; the read-through / write-through wiring into the OCR
// pipeline is exercised end-to-end in
// tests/uat/test_uat_ocr_disk_cache.cpp.
//
// Mapping to ADR 0013 §G13.4 / backlog thresholds:
//   (i)  size-capped LRU eviction, total stays <= ceiling  -> lruEviction*
//   (ii) changed pixels (new hash) miss; unchanged hit      -> contentHashKeyIsInvalidation
//   (iii)reopen restores without re-OCR                     -> survivesReopen / roundTrip
//   plus cross-document dedup, corruption handling, remove.

#include "document/OcrDiskCache.h"
#include "document/SelectableTextStore.h"
#include "ml/OcrEngine.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPolygon>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <vector>

using namespace trailer;

namespace {

OcrEngine::TextBlock makeBlock(const QString &text, QPolygon poly, float conf = 0.9f) {
    OcrEngine::TextBlock b;
    b.text = text;
    b.polygon = std::move(poly);
    b.confidence = conf;
    return b;
}

std::vector<OcrEngine::TextBlock> sampleBlocks(const QString &tag) {
    return {makeBlock(tag + QStringLiteral("-hello"),
                      QPolygon({{0, 0}, {100, 0}, {100, 20}, {0, 20}}), 0.88f),
            makeBlock(tag + QStringLiteral("-world"),
                      QPolygon({{0, 30}, {120, 30}, {120, 55}, {0, 55}}), 0.77f)};
}

// A block whose serialized size is roughly `approxBytes` so eviction can
// be driven with a handful of predictable entries.
std::vector<OcrEngine::TextBlock> bigBlock(int approxBytes) {
    return {makeBlock(QString(approxBytes, QLatin1Char('x')),
                      QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))};
}

QImage solid(int w, int h, QColor c) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    return img;
}

} // namespace

class TestOcrDiskCache : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void roundTripStoresAndLoads();
    void missReturnsNullopt();
    void emptyBlocksAreNotStored();
    void contentHashKeyIsInvalidation();
    void crossDocumentDedup();
    void removeDropsEntry();
    void lruEvictionKeepsUnderCeiling();
    void lruEvictsLeastRecentlyUsed();
    void survivesReopen();
    void corruptFileIsDiscardedAsMiss();

  private:
    QTemporaryDir m_tmp;
    QString m_dir;
};

void TestOcrDiskCache::init() {
    QVERIFY(m_tmp.isValid());
    // Fresh subdirectory per test so state never leaks between slots.
    static int counter = 0;
    m_dir = m_tmp.filePath(QStringLiteral("cache%1").arg(counter++));
}

void TestOcrDiskCache::roundTripStoresAndLoads() {
    OcrDiskCache cache(m_dir);
    const std::uint64_t hash = 0xABCDEF12ULL;
    cache.store(hash, sampleBlocks(QStringLiteral("a")));
    QVERIFY(cache.contains(hash));

    auto loaded = cache.load(hash);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), size_t(2));
    QCOMPARE((*loaded)[0].text, QStringLiteral("a-hello"));
    QCOMPARE((*loaded)[1].text, QStringLiteral("a-world"));
    // Geometry and confidence round-trip.
    QCOMPARE((*loaded)[1].polygon, QPolygon({{0, 30}, {120, 30}, {120, 55}, {0, 55}}));
    QCOMPARE((*loaded)[0].confidence, 0.88f);
}

void TestOcrDiskCache::missReturnsNullopt() {
    OcrDiskCache cache(m_dir);
    QVERIFY(!cache.load(0x999ULL).has_value());
    QVERIFY(!cache.contains(0x999ULL));
}

void TestOcrDiskCache::emptyBlocksAreNotStored() {
    OcrDiskCache cache(m_dir);
    cache.store(0x1ULL, {});
    QVERIFY(!cache.contains(0x1ULL));
    QVERIFY(!cache.load(0x1ULL).has_value());
    QCOMPARE(cache.totalBytes(), std::int64_t(0));
}

void TestOcrDiskCache::contentHashKeyIsInvalidation() {
    // Threshold 3 / ADR G13.4(ii): the content-hash key IS the
    // invalidation. A page whose pixels change renders to a new hash and
    // MISSES; the unchanged page keeps hitting. We use the real
    // hashImageContent() to prove the keys the pipeline actually computes
    // behave this way.
    OcrDiskCache cache(m_dir);
    const QImage pageV1 = solid(64, 48, Qt::white);
    QImage pageV2 = solid(64, 48, Qt::white);
    {
        QPainter p(&pageV2);
        p.fillRect(4, 4, 8, 8, Qt::black); // an edit — different pixels
    }
    const std::uint64_t hV1 = hashImageContent(pageV1);
    const std::uint64_t hV2 = hashImageContent(pageV2);
    QVERIFY(hV1 != hV2);

    cache.store(hV1, sampleBlocks(QStringLiteral("v1")));

    // Unchanged page -> hit.
    QVERIFY(cache.load(hV1).has_value());
    // Changed page -> miss (never served stale), no explicit wipe needed.
    QVERIFY(!cache.load(hV2).has_value());

    // Reverting to identical pixels re-hashes to hV1 and hits again — which
    // is correct: identical pixels genuinely have the same text.
    const std::uint64_t hV1Again = hashImageContent(solid(64, 48, Qt::white));
    QCOMPARE(hV1Again, hV1);
    QVERIFY(cache.load(hV1Again).has_value());
}

void TestOcrDiskCache::crossDocumentDedup() {
    // Threshold 1: keyed by content hash ALONE, so two different
    // "documents" whose pages render to identical pixels share one entry.
    OcrDiskCache cache(m_dir);
    const std::uint64_t shared = hashImageContent(solid(32, 32, QColor(10, 20, 30)));
    cache.store(shared, sampleBlocks(QStringLiteral("shared")));
    QCOMPARE(cache.entryCount(), size_t(1));

    // A second document rendering the identical page computes the same key.
    const std::uint64_t sameKey = hashImageContent(solid(32, 32, QColor(10, 20, 30)));
    QCOMPARE(sameKey, shared);
    auto loaded = cache.load(sameKey);
    QVERIFY(loaded.has_value());
    QCOMPARE((*loaded)[0].text, QStringLiteral("shared-hello"));
    // Still a single entry — no duplication across documents.
    QCOMPARE(cache.entryCount(), size_t(1));
}

void TestOcrDiskCache::removeDropsEntry() {
    OcrDiskCache cache(m_dir);
    cache.store(0x5ULL, sampleBlocks(QStringLiteral("r")));
    QVERIFY(cache.contains(0x5ULL));
    const std::int64_t before = cache.totalBytes();
    QVERIFY(before > 0);
    cache.remove(0x5ULL);
    QVERIFY(!cache.contains(0x5ULL));
    QVERIFY(!cache.load(0x5ULL).has_value());
    QCOMPARE(cache.totalBytes(), std::int64_t(0));
    // Idempotent.
    cache.remove(0x5ULL);
}

void TestOcrDiskCache::lruEvictionKeepsUnderCeiling() {
    // Threshold 2 / ADR G13.4(i): inserting past the ceiling evicts LRU so
    // total on-disk size stays <= ceiling — never unbounded. Drive it with
    // a small ceiling and ~1 KB entries.
    const std::int64_t entryPayload = 1000; // ~1 KB text per entry
    const std::int64_t ceiling = 4096;      // 4 KB — holds only a few
    OcrDiskCache cache(m_dir, ceiling);

    for (std::uint64_t i = 1; i <= 20; ++i) {
        cache.store(i, bigBlock(int(entryPayload)));
        QVERIFY2(cache.totalBytes() <= ceiling,
                 qPrintable(QStringLiteral("totalBytes %1 exceeded ceiling %2 after insert %3")
                                .arg(cache.totalBytes()).arg(ceiling).arg(i)));
    }

    // The actual bytes on disk must also be within the ceiling (accounting
    // is not lying about what it wrote).
    std::int64_t onDisk = 0;
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList(QDir::Files))
        onDisk += fi.size();
    QVERIFY2(onDisk <= ceiling,
             qPrintable(QStringLiteral("on-disk bytes %1 exceeded ceiling %2")
                            .arg(onDisk).arg(ceiling)));
    // And it is bounded — far fewer than the 20 we inserted survive.
    QVERIFY(cache.entryCount() < 20);
}

void TestOcrDiskCache::lruEvictsLeastRecentlyUsed() {
    // The entry EVICTED is the least-recently-USED, not merely the oldest
    // written: touching an old entry via load() protects it.

    // Measure one entry's real serialized size so the ceiling holds
    // exactly three entries regardless of QDataStream encoding details.
    std::int64_t entrySize = 0;
    {
        OcrDiskCache probe(m_tmp.filePath(QStringLiteral("probe")));
        probe.store(1, bigBlock(1000));
        entrySize = probe.totalBytes();
    }
    QVERIFY(entrySize > 0);

    OcrDiskCache cache(m_dir, entrySize * 3); // holds exactly 3
    cache.store(1, bigBlock(1000));
    cache.store(2, bigBlock(1000));
    cache.store(3, bigBlock(1000));
    QVERIFY(cache.contains(1) && cache.contains(2) && cache.contains(3));
    QCOMPARE(cache.entryCount(), size_t(3));

    // Touch entry 1 so it becomes most-recently-used; entry 2 is now LRU.
    QVERIFY(cache.load(1).has_value());

    // Insert a 4th; eviction should drop entry 2 (the LRU), keeping 1.
    cache.store(4, bigBlock(1000));
    QVERIFY2(cache.contains(1), "recently-touched entry 1 must survive eviction");
    QVERIFY2(!cache.contains(2), "least-recently-used entry 2 must be evicted");
    QVERIFY(cache.contains(3));
    QVERIFY(cache.contains(4));
    QVERIFY(cache.totalBytes() <= entrySize * 3);
}

void TestOcrDiskCache::survivesReopen() {
    // Threshold / G13.4(iii): OCR survives a process restart. A second
    // cache object over the same directory re-indexes the persisted entry
    // and serves it without re-OCR.
    {
        OcrDiskCache cache(m_dir);
        cache.store(0x77ULL, sampleBlocks(QStringLiteral("persist")));
    }
    OcrDiskCache reopened(m_dir);
    QVERIFY(reopened.contains(0x77ULL));
    auto loaded = reopened.load(0x77ULL);
    QVERIFY(loaded.has_value());
    QCOMPARE((*loaded)[0].text, QStringLiteral("persist-hello"));
    QVERIFY(reopened.totalBytes() > 0);
}

void TestOcrDiskCache::corruptFileIsDiscardedAsMiss() {
    OcrDiskCache cache(m_dir);
    cache.store(0x88ULL, sampleBlocks(QStringLiteral("c")));
    // Corrupt the file on disk behind the cache's back.
    const QString name = QStringLiteral("%1.ocrtext").arg(0x88ULL, 16, 16, QLatin1Char('0'));
    QFile f(QDir(m_dir).filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("garbage-not-a-valid-stream");
    f.close();

    // A corrupt entry reads as a miss AND the bad file is dropped so it
    // can't poison later reads.
    QVERIFY(!cache.load(0x88ULL).has_value());
    QVERIFY(!cache.contains(0x88ULL));
    QVERIFY(!QFile::exists(QDir(m_dir).filePath(name)));
}

QTEST_MAIN(TestOcrDiskCache)
#include "test_ocr_disk_cache.moc"
