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
#include "document/PdfAdapter.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPdfDocument>
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

QTEST_MAIN(TestPerfGuiThreadIo)
#include "test_perf_gui_thread_io.moc"
