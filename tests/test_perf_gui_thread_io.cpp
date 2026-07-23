// Structural performance test (b): no file IO on the GUI thread during
// document open.
//
// Target invariant (docs/performance-budgets.md → "The whole UI never
// blocks during long work" / "First-page render must not block on a
// full-file read"): reading a document off disk must not run on the
// GUI/main thread, so a large or slow file never performs its open IO on
// the window's thread.
//
// ARCHITECTURE: the residual QPdfDocument::load — the last whole-file read
// that used to run on the GUI thread at open — now runs on a worker thread
// (PdfDocument::startDocOpen, kicked from the ctor). The worker constructs a
// QPdfDocument (worker affinity), loads it there (its reads therefore run off
// the GUI thread), then moveToThread()s it back for GUI-thread adoption. The
// earlier P0 startup-hang fix (#63) already moved the qpdf parse + all-pages
// annotation sweep off-thread; this test guards the final piece and retires
// the former QSKIP (docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md).
//
// The assertion is STRUCTURAL (thread identity / counts), never wall-clock:
// an InstrumentedIODevice injected into the open records the QThread of every
// read, and none may equal the GUI/calling thread.

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

#include <atomic>

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
    void cleanup() { PdfDocument::setLoadDeviceFactoryForTesting({}); }
    void openIoDoesNotRunOnGuiThread();
    void imageOpenDefersFullDecodeOffGuiThread();
};

// THE INVARIANT (backlog 2026-07-15-offthread-pdf-open-placeholder): no read
// performed during a document open driven from the GUI thread lands on the
// GUI/calling thread — InstrumentedIODevice::readThreads() contains no entry
// equal to the GUI thread after the open.
//
// Fail-first: against the pre-change code the QPdfDocument load ran inline in
// the PdfDocument ctor on the calling thread, so the injected device's reads
// were served on the GUI thread and this loop's QVERIFY(t != guiThread) failed
// on the first read. (Confirmed by temporarily forcing the load back onto the
// caller — see the PR notes.)
void TestPerfGuiThreadIo::openIoDoesNotRunOnGuiThread() {
    QThread *guiThread = QThread::currentThread();

    // Read the fixture bytes once on this thread (not part of the measured
    // open); the worker builds its instrumented device from a copy.
    QByteArray bytes;
    {
        QFile f(corpus(QStringLiteral("text_20page.pdf")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        bytes = f.readAll();
    }
    QVERIFY(!bytes.isEmpty());

    // Inject an InstrumentedIODevice into the background document open. The
    // factory is invoked ON THE WORKER THREAD and serves the open's reads, so
    // the recorded read threads reveal exactly where the open IO ran. `captured`
    // is written on the worker and read on this thread only after the open has
    // settled (pageCount() waits for it), so the handoff is safely ordered.
    std::atomic<InstrumentedIODevice *> captured{nullptr};
    PdfDocument::setLoadDeviceFactoryForTesting(
        [&bytes, &captured](const QString &) -> QIODevice * {
            auto *d = new InstrumentedIODevice(bytes);
            d->open(QIODevice::ReadOnly);
            captured.store(d, std::memory_order_release);
            return d;
        });

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = registry.open(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY2(doc != nullptr, "registry must return a document");

    // Forces the (worker) open to complete and be adopted on this thread. The
    // GUI thread only WAITS here; the reads happened on the worker. pageCount
    // resolves definitively — proving the open produced a usable document.
    QCOMPARE(doc->pageCount(), 20);

    InstrumentedIODevice *device = captured.load(std::memory_order_acquire);
    QVERIFY2(device != nullptr, "the injected device must have been used by the open");
    QVERIFY2(device->readCount() > 0, "the IO layer must have served reads during open");

    // The invariant: not a single open read landed on the GUI/calling thread.
    for (QThread *t : device->readThreads()) {
        QVERIFY2(t != guiThread, "document-open IO must not run on the GUI thread");
    }
}

// --- Image path (ADR 0008, accepted for images = Option B). Like the PDF path
// above (now a live off-thread assertion since the off-thread-PDF-open change
// retired its QSKIP), an image open performs NO full-pixel decode on the GUI
// thread: the ctor reads only the header for an immediate contentSizeHint, and
// the full decode runs on a worker. This is a LIVE structural assertion, not a
// skip. Independent, additive slot alongside the PDF assertion.
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
