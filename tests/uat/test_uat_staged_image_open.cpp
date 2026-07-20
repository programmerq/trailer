// UAT evidence + regression guard for staged (async) image open (ADR 0008).
//
// Set TRAILER_STAGED_OPEN_EVIDENCE_DIR to capture the curated before/after
// grab() pair: the honest "Loading image…" placeholder painted immediately at
// open, and the decoded image after the off-GUI-thread decode swaps in — the
// SAME view widget in both shots. Runs offscreen (no display server); the
// asserts are the regression guard, the grabs are the G2 evidence.
//
// Registered across the {1, 1.5, 2} devicePixelRatio matrix (#92) so the
// staged-open view holds on HiDPI as well as dpr=1.

#include "document/ImageAdapter.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

QString evidenceDir() {
    return QString::fromLocal8Bit(qgetenv("TRAILER_STAGED_OPEN_EVIDENCE_DIR"));
}

// Grab `w` offscreen and write it under TRAILER_STAGED_OPEN_EVIDENCE_DIR when
// that env var is set; a no-op otherwise so the regression guard runs on every
// UAT invocation without producing files.
// NB: deliberately does NOT spin the event loop — the caller controls when
// the decode is allowed to land, so the "before" grab captures the real
// placeholder (a processEvents() here would deliver the worker's finished
// signal and swap in the decoded image before the placeholder is grabbed).
void saveShot(QWidget *w, const QString &name) {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QPixmap pm = w->grab();
    QVERIFY2(!pm.isNull(), qPrintable(QStringLiteral("grab returned null for %1").arg(name)));
    QVERIFY2(pm.save(QDir(dir).filePath(name)),
             qPrintable(QStringLiteral("failed to write %1").arg(name)));
}

// A recognisable scene so the decoded shot is visibly the real image, not a
// flat fill that could be confused with the placeholder.
QString writeScene(const QString &path) {
    QImage img(420, 300, QImage::Format_ARGB32);
    img.fill(QColor(238, 242, 248));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(QRect(28, 40, 160, 110), QColor(70, 130, 200));
    p.fillRect(QRect(224, 92, 150, 150), QColor(214, 120, 70));
    p.setPen(QPen(QColor(40, 60, 90), 6));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRect(120, 150, 150, 116));
    p.end();
    img.save(path, "PNG");
    return path;
}

} // namespace

class TestUatStagedImageOpen : public QObject {
    Q_OBJECT
  private slots:
    void staged_open_10_placeholderThenDecodedSwap();

  private:
    QTemporaryDir m_scratch;
};

// UAT-VWR-STAGED-010 — opening an image paints an honest "Loading image…"
// placeholder immediately (no GUI-thread full decode), then swaps in the real
// pixmap once the off-thread decode completes.
void TestUatStagedImageOpen::staged_open_10_placeholderThenDecodedSwap() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeScene(m_scratch.filePath(QStringLiteral("scene.png")));

    ImageDocument doc(path);
    // Open defers the full-pixel decode off the GUI thread (ADR 0008).
    QVERIFY2(doc.isDecodePendingForTest(),
             "open must defer the full-pixel decode off the GUI thread");
    // The window can be sized immediately from the header-only size hint.
    QCOMPARE(doc.contentSizeHint(), QSize(420, 300));

    auto *host = new QWidget;
    host->resize(500, 360);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget *view = doc.createView(host);
    layout->addWidget(view);

    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY(scroll);
    auto *label = qobject_cast<QLabel *>(scroll->widget());
    QVERIFY(label);

    // BEFORE: an honest, visible loading state — not a blank/stub the user
    // could mistake for an empty or broken file (G3).
    QVERIFY2(label->pixmap().isNull(), "placeholder must not show a decoded pixmap yet");
    QVERIFY2(label->text().contains(QStringLiteral("Loading")),
             qPrintable(QStringLiteral("expected a loading placeholder, got '%1'")
                            .arg(label->text())));
    saveShot(host, QStringLiteral("staged-open-01-loading-placeholder.png"));

    // AFTER: the off-thread decode swaps the placeholder for the real image.
    QVERIFY(doc.awaitDecodeForTest());
    QVERIFY2(!label->pixmap().isNull(), "decoded pixmap must replace the placeholder");
    QVERIFY(label->text().isEmpty());
    QApplication::processEvents(); // settle the swap + initial-fit tick before the grab
    saveShot(host, QStringLiteral("staged-open-02-decoded-image.png"));

    delete host;
}

QTEST_MAIN(TestUatStagedImageOpen)
#include "test_uat_staged_image_open.moc"
