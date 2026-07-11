// Structural performance test (b): no synchronous file IO on the GUI
// thread during document open.
//
// Target invariant (docs/performance-budgets.md → "The whole UI never
// blocks during long work"): reading a document off disk should not run
// on the GUI/main thread, so a large or slow file never freezes the
// window while it opens.
//
// CURRENT ARCHITECTURE (the honest reality this test documents):
// document open is fully SYNCHRONOUS on the calling thread —
// DocumentRegistry::open() → PdfAdapter::open() → the PdfDocument ctor,
// which calls QPdfDocument::load(path) and PdfEditor::load(path) inline.
// MainWindow invokes DocumentRegistry::open on the GUI thread, so today
// that file IO happens ON the GUI thread. There is no worker-thread open
// seam yet.
//
// Rather than force a passing assertion that contradicts that reality,
// this test (1) makes a live, TRUE assertion that the PDF IO layer does
// its reads on the calling thread (the mechanism that makes the gap
// real), and (2) QSKIPs the target off-GUI-thread invariant with a
// message naming the gap. All assertions are STRUCTURAL (thread identity
// / counts), never wall-clock.

#include "perf_iodevice.h"

#include "document/DocumentRegistry.h"
#include "document/PdfAdapter.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPdfDocument>
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
};

void TestPerfGuiThreadIo::openIsSynchronousOnCallingThread() {
    QThread *guiThread = QThread::currentThread();

    // --- Reality check 1: the real open path is synchronous on the
    // calling thread. After DocumentRegistry::open() returns on this
    // (the "GUI") thread, the document is already fully loaded — proof
    // the read happened inline, on this thread, not deferred to a worker.
    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = registry.open(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY2(doc != nullptr, "registry must return a document");
    QCOMPARE(doc->pageCount(), 20); // fully loaded synchronously, inline

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
    // thread — does not hold in the current architecture (open is
    // synchronous on the caller, and MainWindow calls it on the GUI
    // thread). Document it as a skip instead of a red test or a false
    // green.
    QSKIP("off-GUI-thread document open is not yet implemented: "
          "DocumentRegistry::open -> PdfAdapter::open -> PdfDocument ctor read the file "
          "synchronously on the calling thread, which is the GUI thread in MainWindow. "
          "Target invariant: perform document-open IO off the GUI thread "
          "(docs/performance-budgets.md 'The whole UI never blocks during long work'). "
          "Tracked as a perf follow-up.");
}

QTEST_MAIN(TestPerfGuiThreadIo)
#include "test_perf_gui_thread_io.moc"
