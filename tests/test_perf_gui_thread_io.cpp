// Structural performance test (b): no synchronous file IO on the GUI
// thread during document open.
//
// Target invariant (docs/performance-budgets.md → "The whole UI never
// blocks during long work"): reading a document off disk should not run
// on the GUI/main thread, so a large or slow file never freezes the
// window while it opens.
//
// CURRENT ARCHITECTURE (the honest reality this test documents):
// document open is still SYNCHRONOUS on the calling thread —
// DocumentRegistry::open() → PdfAdapter::open() → the PdfDocument ctor.
// As of the P0 startup-hang fix (closed by #63; residual tracked in
// docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md) the two heavy
// whole-document passes — the qpdf PdfEditor::load(processFile) parse and
// the all-pages readAnnotations() sweep — are now LAZY: they no longer
// run in the ctor, so they are off the synchronous open path. What
// remains on the open path is QPdfDocument::load(path)'s bounded
// progressive read (~16ms on a 115MB/8000-page file — it yields
// pageCount + a page-0 render without consuming the whole file).
// MainWindow still invokes DocumentRegistry::open on the GUI thread, so
// that residual bounded read happens ON the GUI thread: there is no
// worker-thread open seam yet. Moving it off-thread behind a first-page
// placeholder is the deferred follow-up
// (docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md).
//
// Rather than force a passing assertion that contradicts that reality,
// this test (1) makes a live, TRUE assertion that the PDF IO layer does
// its reads on the calling thread (the mechanism that makes the gap
// real), and (2) QSKIPs the target off-GUI-thread invariant with a
// message naming the gap. All assertions are STRUCTURAL (thread identity
// / counts), never wall-clock.

#include "perf_iodevice.h"

#include "document/DocumentRegistry.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPdfDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

using namespace trailer;
using trailer::perf::InstrumentedIODevice;

namespace {
QString corpus(const QString &name) {
    return QStringLiteral("%1/%2").arg(QStringLiteral(TRAILER_PERF_CORPUS_DIR), name);
}
} // namespace

class TestPerfGuiThreadIo : public QObject {
    Q_OBJECT
  private slots:
    void openIsSynchronousOnCallingThread();
    void imageOpenDefersFullDecodeOffGuiThread();
};

void TestPerfGuiThreadIo::openIsSynchronousOnCallingThread() {
    QThread *guiThread = QThread::currentThread();

    // --- Reality check 1: the real open path is synchronous on the
    // calling thread. After DocumentRegistry::open() returns on this
    // (the "GUI") thread, QPdfDocument's bounded progressive load has
    // already run inline — pageCount is available with no worker hop.
    // (The qpdf parse + annotation sweep are now lazy, so they are NOT
    // part of this synchronous open; only QPdfDocument's read is.)
    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = registry.open(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY2(doc != nullptr, "registry must return a document");
    QCOMPARE(doc->pageCount(), 20); // resolved synchronously, inline, on this thread

    // --- Reality check 2 (live assertion): the PDF IO layer serves every
    // read on whichever thread drives it — it spins up no worker of its
    // own. Driven here from the GUI/main thread, every recorded read is
    // on the GUI thread. This is the mechanism by which a GUI-thread
    // open() performs GUI-thread IO.
    QFile f(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    auto *device = new InstrumentedIODevice(bytes);
    QVERIFY(device->open(QIODevice::ReadOnly));

    QPdfDocument pdf;
    pdf.load(device);
    for (int i = 0; i < 100 && pdf.status() != QPdfDocument::Status::Ready; ++i) {
        QCoreApplication::processEvents();
    }
    QCOMPARE(pdf.status(), QPdfDocument::Status::Ready);
    (void)pdf.render(0, QSize(612, 792));

    QVERIFY2(device->readCount() > 0, "the IO layer must have served reads");
    for (QThread *t : device->readThreads()) {
        QCOMPARE(t, guiThread); // every read landed on the calling (GUI) thread
    }

    // --- The gap. The target invariant — that this IO run OFF the GUI
    // thread — still does not hold. The P0 startup-hang fix removed the
    // two heavy whole-document passes (qpdf processFile parse + all-pages
    // annotation sweep) from the open path by making them lazy, but the
    // residual QPdfDocument bounded progressive read is still synchronous
    // on the caller, which is the GUI thread in MainWindow. Moving that
    // last read off-thread behind a first-page placeholder is the
    // deferred follow-up, so this stays a documented skip rather than a
    // red test or a false green.
    QSKIP("off-GUI-thread document open is not yet implemented: the P0 startup-hang fix made "
          "the qpdf editor parse and the all-pages annotation sweep lazy (off the open path), "
          "but DocumentRegistry::open -> PdfAdapter::open -> PdfDocument ctor still runs "
          "QPdfDocument's bounded progressive read synchronously on the calling thread, which "
          "is the GUI thread in MainWindow. Target invariant: perform the residual "
          "document-open IO off the GUI thread behind a placeholder first page "
          "(docs/performance-budgets.md 'The whole UI never blocks during long work'). "
          "Tracked as docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md.");
}

// --- Image path (ADR 0008, accepted for images = Option B). Unlike the PDF
// path above (still a documented QSKIP gap), an image open performs NO
// full-pixel decode on the GUI thread: the ctor reads only the header for an
// immediate contentSizeHint, and the full decode runs on a worker. This is a
// LIVE structural assertion, not a skip — the image half of the target
// invariant already holds. Additive slot so it rebases cleanly alongside the
// sibling off-thread PDF PR that also edits this file.
void TestPerfGuiThreadIo::imageOpenDefersFullDecodeOffGuiThread() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("big.png"));
    // A non-trivial image so a full decode would be a visible GUI-thread cost.
    QImage img(1024, 768, QImage::Format_ARGB32);
    img.fill(Qt::green);
    QVERIFY(img.save(path, "PNG"));

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<ImageAdapter>());
    auto doc = registry.open(path);
    QVERIFY2(doc != nullptr, "registry must return an image document");

    // Proxy 1: the size hint is available immediately from a header-only read,
    // with no full-pixel decode on this (GUI) thread.
    QCOMPARE(doc->contentSizeHint(), QSize(1024, 768));
    QCOMPARE(doc->pageCount(), 1); // known from the header, before any decode

    // Proxy 2: the full-pixel decode has NOT run on the calling thread — it is
    // still pending on a worker the instant open() returns.
    auto *imageDoc = dynamic_cast<ImageDocument *>(doc.get());
    QVERIFY2(imageDoc != nullptr, "expected an ImageDocument");
    QVERIFY2(imageDoc->isDecodePendingForTest(),
             "image full-pixel decode must be deferred off the GUI thread, "
             "not run synchronously at open");

    // Proxy 3: it completes off-thread once the event loop turns.
    QVERIFY2(imageDoc->awaitDecodeForTest(), "the worker decode must complete");
}

QTEST_MAIN(TestPerfGuiThreadIo)
#include "test_perf_gui_thread_io.moc"
