// UAT harness — Off-GUI-thread document open (UAT-VWR-100)
//
// Curated before/after grab() evidence for the placeholder-then-page open
// flow introduced by the backlog item
// docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md (deferred (b) of
// decision record 0006). The residual QPdfDocument::load runs on a worker
// thread; until it settles the document view is an honest "Loading…"
// placeholder, then it is replaced in place by the real page view.
//
// Set TRAILER_OFFTHREAD_EVIDENCE_DIR to a directory to have this slot write
// its before/after PNGs there (the G2 evidence). Otherwise it asserts the
// wired behaviour like any other uat slot. Runs across the HiDPI dpr matrix
// {1, 1.5, 2} because open-at-logical-size is dpr-sensitive.

#include "document/PdfAdapter.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QLabel>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest/QtTest>

#include <memory>

using namespace trailer;

namespace {

QString evidenceDir() {
    return QString::fromLocal8Bit(qgetenv("TRAILER_OFFTHREAD_EVIDENCE_DIR"));
}

// Grab `w` offscreen and write it under TRAILER_OFFTHREAD_EVIDENCE_DIR when set.
void saveShot(QWidget *w, const QString &name) {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    const QPixmap pm = w->grab();
    QVERIFY2(!pm.isNull(), qPrintable(QStringLiteral("grab returned null for %1").arg(name)));
    QVERIFY2(pm.save(QDir(dir).filePath(name)),
             qPrintable(QStringLiteral("failed to write %1").arg(name)));
}

// A read-only device over an in-memory buffer whose FIRST read blocks on a
// semaphore, so the background open stays in flight until the test releases it.
// This makes the "Loading…" placeholder deterministically observable.
class GatedDevice : public QIODevice {
  public:
    GatedDevice(QByteArray data, std::shared_ptr<QSemaphore> gate)
        : m_data(std::move(data)), m_gate(std::move(gate)) {}
    bool isSequential() const override { return false; }
    qint64 size() const override { return m_data.size(); }

  protected:
    qint64 readData(char *out, qint64 maxSize) override {
        if (!m_released) {
            m_gate->acquire(); // blocks the worker until the test releases it
            m_released = true;
        }
        const qint64 p = pos();
        const qint64 available = m_data.size() - p;
        const qint64 n = std::min(maxSize, available);
        if (n <= 0)
            return 0;
        memcpy(out, m_data.constData() + p, static_cast<size_t>(n));
        return n;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

  private:
    QByteArray m_data;
    std::shared_ptr<QSemaphore> m_gate;
    bool m_released = false;
};

// Build a small multi-page PDF with visible text so the loaded grab shows a
// real page rather than a blank one.
QString makePdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    for (int i = 0; i < 2; ++i) {
        painter.drawText(QRect(200, 200, 3000, 600), Qt::AlignCenter,
                         QStringLiteral("Off-thread open — page %1").arg(i + 1));
        if (i == 0)
            writer.newPage();
    }
    painter.end();
    return path;
}

} // namespace

class TestUatOffthreadOpen : public QObject {
    Q_OBJECT
  private slots:
    void cleanup() { PdfDocument::setLoadDeviceFactoryForTesting({}); }
    void uat_vwr_100_placeholderThenPage();
};

void TestUatOffthreadOpen::uat_vwr_100_placeholderThenPage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makePdf(dir.filePath(QStringLiteral("offthread.pdf")));

    QByteArray bytes;
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        bytes = f.readAll();
    }
    QVERIFY(!bytes.isEmpty());

    // Gate the worker's first read so the open stays in flight while we capture
    // the placeholder. Released below to let the open settle.
    auto gate = std::make_shared<QSemaphore>(0);
    PdfDocument::setLoadDeviceFactoryForTesting([bytes, gate](const QString &) -> QIODevice * {
        auto *d = new GatedDevice(bytes, gate);
        d->open(QIODevice::ReadOnly);
        return d;
    });

    // Construct directly (not via the adapter's needsPassword() prompt loop,
    // which would force the open to settle) so createView genuinely runs while
    // the open is still in flight.
    PdfDocument doc(path);

    // Host the view in a shown top-level so grabs and layout are realistic.
    auto host = std::make_unique<QWidget>();
    host->resize(800, 600);
    auto *hostLayout = new QVBoxLayout(host.get());
    hostLayout->setContentsMargins(0, 0, 0, 0);
    QWidget *view = doc.createView(host.get());
    QVERIFY(view != nullptr);
    hostLayout->addWidget(view);
    host->show();
    QApplication::processEvents();

    // BEFORE: the open has not settled, so the view is the honest "Loading…"
    // placeholder — no QPdfView yet, and a visible "Loading…" label.
    QVERIFY2(view->findChild<QPdfView *>() == nullptr,
             "the real page view must not exist while the open is in flight");
    auto *placeholder = view->findChild<QLabel *>(QStringLiteral("pdfLoadingPlaceholder"));
    QVERIFY2(placeholder != nullptr, "a 'Loading…' placeholder must be shown during open");
    QCOMPARE(placeholder->text(), QStringLiteral("Loading…"));
    saveShot(host.get(), QStringLiteral("uat-vwr-100-before-loading.png"));

    // Release the gated read; the worker finishes, the finished slot adopts the
    // document on this (GUI) thread, and buildRealView swaps the placeholder for
    // the real page view.
    gate->release();
    QTRY_VERIFY(view->findChild<QPdfView *>() != nullptr);
    QVERIFY2(view->findChild<QLabel *>(QStringLiteral("pdfLoadingPlaceholder")) == nullptr,
             "the placeholder must be gone once the page view is shown");
    QVERIFY(doc.isValid());
    QCOMPARE(doc.pageCount(), 2);
    QApplication::processEvents();
    QTest::qWait(50);
    saveShot(host.get(), QStringLiteral("uat-vwr-100-after-page.png"));
}

QTEST_MAIN(TestUatOffthreadOpen)
#include "test_uat_offthread_open.moc"
