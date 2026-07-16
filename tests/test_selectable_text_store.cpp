// Unit tests for SelectableTextStore — the per-document, per-page
// OCR cache that drives SelectableTextLayer. Round-trip, invalidation,
// signal emission, and content-hash invalidation are covered here.
//
// Sister UAT slots in tests/uat/test_uat_recognize_text.cpp drive the
// store through the real OCR pipeline; these tests focus on the
// pure container behaviour.

#include "document/SelectableTextStore.h"

#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

OcrEngine::TextBlock makeBlock(const QString &text, QPolygon poly, float conf = 0.9f) {
    OcrEngine::TextBlock b;
    b.text = text;
    b.polygon = std::move(poly);
    b.confidence = conf;
    return b;
}

QImage makeTestImage(int w, int h, QColor fill) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(fill);
    return img;
}

} // namespace

class TestSelectableTextStore : public QObject {
    Q_OBJECT
  private slots:
    void emptyStoreHasNoResults();
    void putRoundTripsBlocks();
    void replacingEntryReplacesBlocks();
    void invalidateDropsEntry();
    void clearDropsEverything();
    void signalsFireOnChanges();
    void hashIsStableAndUnique();
    void hashContentSurvivesFormatConversion();
    void attemptedMemoTracksHashAndClears();
};

void TestSelectableTextStore::emptyStoreHasNoResults() {
    SelectableTextStore store;
    QVERIFY(!store.hasResults(0));
    QVERIFY(!store.hasResults(5));
    QCOMPARE(store.blocks(0).size(), size_t(0));
    QCOMPARE(store.contentHashFor(0), uint64_t(0));
}

void TestSelectableTextStore::putRoundTripsBlocks() {
    SelectableTextStore store;
    std::vector<OcrEngine::TextBlock> blocks;
    blocks.push_back(makeBlock("hello", QPolygon({{0, 0}, {100, 0}, {100, 20}, {0, 20}})));
    blocks.push_back(makeBlock("world", QPolygon({{0, 30}, {100, 30}, {100, 50}, {0, 50}})));
    store.put(0, 0x12345678ULL, blocks);
    QVERIFY(store.hasResults(0));
    QCOMPARE(store.blocks(0).size(), size_t(2));
    QCOMPARE(store.blocks(0)[0].text, QStringLiteral("hello"));
    QCOMPARE(store.blocks(0)[1].text, QStringLiteral("world"));
    QCOMPARE(store.contentHashFor(0), uint64_t(0x12345678ULL));
    // Other pages still empty.
    QVERIFY(!store.hasResults(1));
}

void TestSelectableTextStore::replacingEntryReplacesBlocks() {
    SelectableTextStore store;
    store.put(2, 1ULL,
              {makeBlock("first", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    QCOMPARE(store.blocks(2).size(), size_t(1));
    store.put(2, 2ULL,
              {makeBlock("second", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}})),
               makeBlock("third", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    QCOMPARE(store.blocks(2).size(), size_t(2));
    QCOMPARE(store.blocks(2)[0].text, QStringLiteral("second"));
    QCOMPARE(store.contentHashFor(2), uint64_t(2));
}

void TestSelectableTextStore::invalidateDropsEntry() {
    SelectableTextStore store;
    store.put(0, 1ULL,
              {makeBlock("x", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    QVERIFY(store.hasResults(0));
    store.invalidate(0);
    QVERIFY(!store.hasResults(0));
    QCOMPARE(store.contentHashFor(0), uint64_t(0));
    // Invalidating an empty page is a no-op (should not crash).
    store.invalidate(99);
}

void TestSelectableTextStore::clearDropsEverything() {
    SelectableTextStore store;
    store.put(0, 1ULL,
              {makeBlock("a", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    store.put(1, 2ULL,
              {makeBlock("b", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    store.clear();
    QVERIFY(!store.hasResults(0));
    QVERIFY(!store.hasResults(1));
}

void TestSelectableTextStore::signalsFireOnChanges() {
    SelectableTextStore store;
    QSignalSpy pageChangedSpy(&store, &SelectableTextStore::pageChanged);
    QSignalSpy changedSpy(&store, &SelectableTextStore::changed);
    store.put(3, 1ULL,
              {makeBlock("x", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    QCOMPARE(pageChangedSpy.count(), 1);
    QCOMPARE(pageChangedSpy.at(0).at(0).toInt(), 3);
    QCOMPARE(changedSpy.count(), 1);
    store.invalidate(3);
    QCOMPARE(pageChangedSpy.count(), 2);
    QCOMPARE(pageChangedSpy.at(1).at(0).toInt(), 3);
    QCOMPARE(changedSpy.count(), 2);
    // Invalidating an unrelated page emits nothing.
    store.invalidate(7);
    QCOMPARE(pageChangedSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 2);
    // Clear emits only the bulk-changed signal.
    store.put(0, 1ULL,
              {makeBlock("y", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    store.clear();
    QCOMPARE(changedSpy.count(), 4); // one for put, one for clear
}

void TestSelectableTextStore::hashIsStableAndUnique() {
    const QImage a = makeTestImage(64, 64, Qt::white);
    const QImage b = makeTestImage(64, 64, Qt::white);
    QImage c = makeTestImage(64, 64, Qt::white);
    QPainter p(&c);
    p.fillRect(10, 10, 5, 5, Qt::black);
    p.end();
    const QImage d = makeTestImage(65, 64, Qt::white); // different size
    const QImage nothing;

    QCOMPARE(hashImageContent(a), hashImageContent(b));
    QVERIFY(hashImageContent(a) != hashImageContent(c));
    QVERIFY(hashImageContent(a) != hashImageContent(d));
    QCOMPARE(hashImageContent(nothing), uint64_t(0));
    // Never returns zero for a non-empty image.
    QVERIFY(hashImageContent(a) != uint64_t(0));
}

void TestSelectableTextStore::hashContentSurvivesFormatConversion() {
    const QImage rgb = makeTestImage(32, 32, QColor(20, 40, 60));
    const QImage rgba = rgb.convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(hashImageContent(rgb), hashImageContent(rgba));
}

void TestSelectableTextStore::attemptedMemoTracksHashAndClears() {
    // The attempted-and-empty memo (ADR G13.1/G13.2) lets the ambient OCR
    // path skip re-OCRing a text-less page without ever making it report a
    // text layer. It is keyed by (page, contentHash) and is independent of
    // hasResults().
    SelectableTextStore store;
    QVERIFY(!store.wasAttempted(0, 0xABCULL));

    store.markAttempted(0, 0xABCULL);
    // Memo hits only for the recorded hash — an edited page (new hash)
    // re-OCRs.
    QVERIFY(store.wasAttempted(0, 0xABCULL));
    QVERIFY(!store.wasAttempted(0, 0xDEFULL));
    // Honesty: the memo must NOT masquerade as results.
    QVERIFY(!store.hasResults(0));
    QCOMPARE(store.blocks(0).size(), size_t(0));
    QCOMPARE(store.contentHashFor(0), uint64_t(0));
    // Other pages are unaffected.
    QVERIFY(!store.wasAttempted(1, 0xABCULL));

    // invalidate(page) drops the memo so a force-rerun / edit re-OCRs.
    store.invalidate(0);
    QVERIFY(!store.wasAttempted(0, 0xABCULL));

    // clear() drops every memo.
    store.markAttempted(2, 0x111ULL);
    store.markAttempted(3, 0x222ULL);
    store.clear();
    QVERIFY(!store.wasAttempted(2, 0x111ULL));
    QVERIFY(!store.wasAttempted(3, 0x222ULL));

    // A real put() for a page and its attempted memo are independent:
    // putting results does not require the memo, and hasResults stays the
    // authoritative "has a text layer" signal.
    store.markAttempted(4, 0x333ULL);
    store.put(4, 0x444ULL,
              {makeBlock("real", QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
    QVERIFY(store.hasResults(4));
    // invalidate clears both the results entry and the memo.
    store.invalidate(4);
    QVERIFY(!store.hasResults(4));
    QVERIFY(!store.wasAttempted(4, 0x333ULL));
}

QTEST_MAIN(TestSelectableTextStore)
#include "test_selectable_text_store.moc"
