// UAT harness — Bounded on-disk OCR cache (ADR 0013 §G13.4).
//
// These slots drive the REAL OcrController read-through / write-through
// path (the same submitPage() MainWindow uses) against an injected,
// temp-directory OcrDiskCache, proving the acceptance thresholds of the
// backlog item docs/backlog/2026-07-15-bounded-ondisk-ocr-cache.md end to
// end — not just at the cache-container level (that is covered by the unit
// test tests/test_ocr_disk_cache.cpp):
//
//   uat_ocrcache_010_reopenRestoresWithoutReOcr
//       Threshold 1 / ADR G13.4(iii): after a page is recognized once and
//       written through to disk, a reopen (in-memory store cleared)
//       restores the text WITHOUT re-running OCR — the recognizer call
//       count does not advance (deterministic 0-re-recognitions proxy).
//
//   uat_ocrcache_020_dedupHitsChangedContentMisses
//       Thresholds 1 + 3 / ADR G13.4(ii)(iii): the disk key is the content
//       hash ALONE, so a *different document* whose page renders to
//       identical pixels is served from disk (cross-doc dedup, no re-OCR),
//       while a document whose pixels differ MISSES and re-OCRs. This is
//       the invalidation-by-construction the cache relies on for external
//       change (a changed file re-renders to a new hash and cannot be
//       served stale).

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/OcrDiskCache.h"
#include "document/SelectableTextStore.h"
#include "ml/CancellationToken.h"
#include "ml/OcrEngine.h"
#include "settings/Settings.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/OcrController.h"

#include <QDir>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPolygon>
#include <QRect>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QString writeTextImage(const QString &path, const QString &text, int w = 640, int h = 200) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    QFont f;
    f.setPixelSize(80);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(img.rect(), Qt::AlignCenter, text);
    p.end();
    img.save(path, "PNG");
    return path;
}

// A counting recognizer returning one sentinel block. The count is the
// deterministic proxy for "did we re-OCR" — a disk hit must not advance it.
OcrController::RecognizeFn countingRecognizer(std::shared_ptr<std::atomic<int>> calls,
                                              const QString &tag) {
    return [calls, tag](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
        ++*calls;
        OcrEngine::TextBlock b;
        b.text = tag;
        b.polygon = QPolygon(QRect(0, 0, 40, 30));
        return {b};
    };
}

IDocument *openDoc(Application *app, const QString &path) {
    // Route every open into the SAME window so currentMainWindow() (the
    // first top-level MainWindow) always reflects the just-opened document;
    // the default NewWindow mode would spawn a separate frame per file and
    // leave currentMainWindow() pointing at the first (stale) one.
    app->settings().setOpenFilesIn(OpenFilesIn::SameWindow);
    app->openFiles({path});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    if (!mw)
        return nullptr;
    auto *dv = mw->findChild<DocumentView *>();
    return dv ? dv->currentDocument() : nullptr;
}

} // namespace

class TestUatOcrDiskCache : public QObject {
    Q_OBJECT
  private slots:
    void uat_ocrcache_010_reopenRestoresWithoutReOcr();
    void uat_ocrcache_020_dedupHitsChangedContentMisses();

  private:
    QTemporaryDir m_scratch;
};

void TestUatOcrDiskCache::uat_ocrcache_010_reopenRestoresWithoutReOcr() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString img =
        writeTextImage(m_scratch.filePath(QStringLiteral("reopen.png")), QStringLiteral("HELLO"));

    IDocument *doc = openDoc(app, img);
    QVERIFY(doc);
    auto *store = doc->selectableText();
    QVERIFY(store && !store->hasResults(0));

    OcrController controller(app);
    controller.setDocument(doc);
    controller.setModelReadyForTesting(true);
    controller.setProgressRevealDelayMs(0);

    // Isolate the disk tier in a temp directory so we never touch the real
    // data dir, and keep a handle to assert on the persisted entry.
    auto disk = std::make_shared<OcrDiskCache>(m_scratch.filePath(QStringLiteral("cache")));
    controller.setDiskCacheForTesting(disk);

    auto calls = std::make_shared<std::atomic<int>>(0);
    controller.setRecognizerForTesting(countingRecognizer(calls, QStringLiteral("sentinel")));

    // First recognition: the recognizer runs once and the result is written
    // through to disk.
    controller.submitUserPages(doc, {0}, /*forceRerun=*/false);
    QTRY_VERIFY(store->hasResults(0));
    QCOMPARE(calls->load(), 1);
    QCOMPARE(store->blocks(0).front().text, QStringLiteral("sentinel"));

    const quint64 hash = hashImageContent(doc->renderPageForOcr(0));
    QVERIFY2(disk->contains(hash), "recognized page must be written through to the disk cache");

    // Simulate a reopen: the in-memory tier is gone, the disk tier is not.
    store->clear();
    QVERIFY(!store->hasResults(0));

    // Recognize again: the read-through restores the text from disk WITHOUT
    // re-running OCR — the recognizer call count does NOT advance.
    controller.submitUserPages(doc, {0}, /*forceRerun=*/false);
    QTRY_VERIFY(store->hasResults(0));
    QCOMPARE(calls->load(), 1); // 0 re-recognitions
    QCOMPARE(store->blocks(0).front().text, QStringLiteral("sentinel"));
}

void TestUatOcrDiskCache::uat_ocrcache_020_dedupHitsChangedContentMisses() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Two files with IDENTICAL pixels (same QImage saved twice) and one with
    // DIFFERENT pixels.
    QImage same(640, 200, QImage::Format_RGB32);
    same.fill(Qt::white);
    {
        QPainter p(&same);
        QFont f;
        f.setPixelSize(80);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(same.rect(), Qt::AlignCenter, QStringLiteral("ALPHA"));
    }
    const QString aPath = m_scratch.filePath(QStringLiteral("a.png"));
    const QString bPath = m_scratch.filePath(QStringLiteral("b.png"));
    QVERIFY(same.save(aPath, "PNG"));
    QVERIFY(same.save(bPath, "PNG"));
    const QString cPath = writeTextImage(m_scratch.filePath(QStringLiteral("c.png")),
                                         QStringLiteral("DIFFERENT"));

    // One disk cache shared across all three documents (as it is in
    // production — the cache is process-wide, keyed by content hash).
    auto disk = std::make_shared<OcrDiskCache>(m_scratch.filePath(QStringLiteral("shared")));
    auto calls = std::make_shared<std::atomic<int>>(0);

    // Document A: first recognition populates the shared disk cache.
    IDocument *docA = openDoc(app, aPath);
    QVERIFY(docA);
    OcrController controller(app);
    controller.setModelReadyForTesting(true);
    controller.setProgressRevealDelayMs(0);
    controller.setDiskCacheForTesting(disk);
    controller.setRecognizerForTesting(countingRecognizer(calls, QStringLiteral("A")));
    controller.setDocument(docA);
    controller.submitUserPages(docA, {0}, /*forceRerun=*/false);
    QTRY_VERIFY(docA->selectableText()->hasResults(0));
    QCOMPARE(calls->load(), 1);

    // Document B: DIFFERENT document, IDENTICAL pixels -> same content hash
    // -> served from disk, recognizer NOT called again (cross-doc dedup /
    // "unchanged page hits").
    IDocument *docB = openDoc(app, bPath);
    QVERIFY(docB);
    QVERIFY(!docB->selectableText()->hasResults(0));
    controller.setDocument(docB);
    controller.submitUserPages(docB, {0}, /*forceRerun=*/false);
    QTRY_VERIFY(docB->selectableText()->hasResults(0));
    QCOMPARE(calls->load(), 1); // dedup hit — no re-OCR
    QCOMPARE(docB->selectableText()->blocks(0).front().text, QStringLiteral("A"));

    // Document C: DIFFERENT pixels -> different content hash -> MISS -> the
    // recognizer runs ("changed content misses"). This is the same seam
    // that makes an externally-changed file re-OCR instead of serving stale
    // text.
    IDocument *docC = openDoc(app, cPath);
    QVERIFY(docC);
    QVERIFY(!docC->selectableText()->hasResults(0));
    controller.setDocument(docC);
    controller.setRecognizerForTesting(countingRecognizer(calls, QStringLiteral("C")));
    controller.submitUserPages(docC, {0}, /*forceRerun=*/false);
    QTRY_VERIFY(docC->selectableText()->hasResults(0));
    QCOMPARE(calls->load(), 2); // miss — recognizer ran a second time
    QCOMPARE(docC->selectableText()->blocks(0).front().text, QStringLiteral("C"));
}

int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatOcrDiskCache tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_ocr_disk_cache.moc"
