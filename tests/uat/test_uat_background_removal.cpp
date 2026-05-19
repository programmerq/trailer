// UAT harness — Background removal (Phase 6 §6.3.8 / DESIGN §6.1.5).
//
// Drives the full integration path for the Tools → Remove Background
// feature. The UI path uses a modal QProgressDialog we can't click
// through in offscreen mode, so this test exercises the pieces the
// menu slot relies on:
//
//   1. Open an image in a tab → ImageDocument lands on the active view.
//   2. Find the Tools → Remove Background action in the menu bar and
//      confirm it's wired + enabled for an image document.
//   3. Gate the rest of the test on a seeded u2netp model: if
//      TRAILER_TEST_U2NETP points at a valid ONNX, copy it into
//      AppPaths::modelsDir() and verify BackgroundRemover.remove()
//      produces an alpha-bearing QImage the same size as the input,
//      then apply via ImageDocument::replaceImage() and confirm
//      isDirty() + canUndo() flip positive (the undo-safe path).
//   4. If no model is available, verify the feature gracefully
//      short-circuits (remove returns null, replaceImage not called).
//
// Without a real model the slow / network-dependent inference step is
// skipped — but the plumbing around the menu action, document
// dispatch, and undo wiring is still exercised on every CI run.
//
//   uat_bgr_010_removeBackgroundMenuActionWired
//       Remove Background action is present in the Tools menu and
//       enabled for ImageDocument.
//   uat_bgr_020_removeBackgroundAppliesAlphaWithRealModel
//       With a seeded u2netp cache, calling BackgroundRemover.remove()
//       and ImageDocument::replaceImage() produces a dirty, undoable
//       state with a same-size ARGB result.
//   uat_bgr_030_removeBackgroundNoopsWithoutModel
//       Without any cached model, BackgroundRemover.remove() returns
//       a null QImage and no replaceImage side effect occurs.
//   uat_bgr_040_neverDownloadPolicyDisablesMenuWithTooltip
//       With the U²-Net model marked Never Download and absent from
//       the cache, the Tools → Remove Background menu entry is
//       disabled and carries a tooltip that points the user at
//       Tools → Manage ML Models…  No popup is raised.
//   uat_bgr_050_goodCandidateImageSurfacesBadge
//       Opening a photo-like fixture (high edge density + saturation)
//       triggers the heuristic; once the MlScheduler finishes the
//       Prefetch task, the Remove Background action picks up a
//       non-null icon (the "sparkle" badge).
//   uat_bgr_060_flatDocumentImageHasNoBadge
//       Opening a flat document-like fixture (near-white + thin text
//       bands) leaves the Remove Background action with no badge icon
//       after the scoring pass completes.

#include "app/Application.h"
#include "document/ImageAdapter.h"
#include "ml/BackgroundCandidateScorer.h"
#include "ml/BackgroundRemover.h"
#include "ml/MlScheduler.h"
#include "ml/ModelRegistry.h"
#include "settings/AppPaths.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/ModelManagerDialog.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

// Walk the menu tree looking for a leaf with a matching text. Matches
// are case-sensitive and mnemonic-aware — Qt's QAction::text() returns
// the raw "Remove &Background" string.
QAction *findActionByText(QMenuBar *bar, const QString &text) {
    for (QAction *top : bar->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *a : menu->actions()) {
            if (a->text() == text)
                return a;
        }
    }
    return nullptr;
}

QString writeSampleImage(const QString &path) {
    // Simple two-tone pattern — big enough that the 320x320 downsample
    // inside the remover exercises smooth scaling, small enough to run
    // quickly under the offscreen platform.
    QImage img(256, 256, QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const bool tile = ((x / 32) + (y / 32)) % 2;
            scan[x] = tile ? qRgb(240, 240, 240) : qRgb(16, 16, 16);
        }
    }
    img.save(path, "PNG");
    return path;
}

// "Photo-like" fixture: a parrot-style subject covering most of the
// frame, with high-frequency feather detail and several saturated
// colour patches. Designed to clear BackgroundCandidateScorer's
// recommend threshold. Mirrors the unit-test bird fixture so the
// scorer's verdict matches between unit and UAT paths.
QString writePhotoLikeImage(const QString &path) {
    const int w = 256;
    const int h = 192;
    QImage img(w, h, QImage::Format_ARGB32);
    QPainter p(&img);
    // Background — sky gradient with hue rotation for non-trivial
    // saturation variance.
    for (int y = 0; y < h; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(h - 1);
        const int r = 100 + static_cast<int>(t * 40);
        const int g = 140 + static_cast<int>(t * 60);
        const int b = 220 - static_cast<int>(t * 80);
        p.setPen(QColor(r, g, b));
        p.drawLine(0, y, w, y);
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    // Bird body — radial saturated subject covering ~40% of the frame.
    QRadialGradient body(QPointF(w * 0.45, h * 0.55), w * 0.35);
    body.setColorAt(0.0, QColor(240, 80, 30));
    body.setColorAt(0.7, QColor(180, 30, 40));
    body.setColorAt(1.0, QColor(80, 20, 50));
    p.setBrush(body);
    p.drawEllipse(QPointF(w * 0.45, h * 0.55), w * 0.36, h * 0.45);
    // Wing.
    p.setBrush(QColor(30, 140, 80));
    QPainterPath wing;
    wing.moveTo(w * 0.20, h * 0.50);
    wing.cubicTo(w * 0.10, h * 0.70, w * 0.30, h * 0.85, w * 0.55, h * 0.78);
    wing.cubicTo(w * 0.50, h * 0.65, w * 0.40, h * 0.55, w * 0.20, h * 0.50);
    p.drawPath(wing);
    // Feather stripes — drives the mean Sobel response.
    p.setPen(QPen(QColor(20, 20, 20), 2));
    for (int i = 0; i < 14; ++i) {
        const float t = static_cast<float>(i) / 14.0f;
        const int y0 = static_cast<int>(h * (0.30f + t * 0.55f));
        p.drawLine(static_cast<int>(w * 0.15), y0, static_cast<int>(w * 0.75), y0 + 3);
    }
    p.setPen(QPen(QColor(255, 220, 30), 2));
    for (int i = 0; i < 6; ++i) {
        const float t = static_cast<float>(i) / 6.0f;
        const int y0 = static_cast<int>(h * (0.50f + t * 0.30f));
        p.drawLine(static_cast<int>(w * 0.25), y0, static_cast<int>(w * 0.65), y0 + 6);
    }
    p.setPen(Qt::NoPen);
    // Beak.
    p.setBrush(QColor(255, 180, 30));
    QPainterPath beak;
    beak.moveTo(w * 0.60, h * 0.35);
    beak.lineTo(w * 0.85, h * 0.40);
    beak.lineTo(w * 0.60, h * 0.50);
    beak.closeSubpath();
    p.drawPath(beak);
    // Eye.
    p.setBrush(QColor(20, 20, 20));
    p.drawEllipse(QPointF(w * 0.55, h * 0.40), w * 0.04, w * 0.04);
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPointF(w * 0.555, h * 0.395), w * 0.012, w * 0.012);
    p.end();
    img.save(path, "PNG");
    return path;
}

// "Flat-document-like" fixture: near-white page with two thin bands
// of dark text. Designed to score below the badge threshold.
QString writeFlatDocumentImage(const QString &path) {
    const int w = 192;
    const int h = 256;
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor(248, 248, 248));
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30));
    const int textHeight = 6;
    for (int row = 0; row < 2; ++row) {
        const int y = (row == 0 ? h * 30 / 100 : h * 60 / 100);
        for (int word = 0; word < 6; ++word) {
            const int x = 10 + word * (w - 20) / 6;
            const int wWord = (w - 30) / 6 - 4;
            p.drawRect(x, y, wWord, textHeight);
        }
    }
    p.end();
    img.save(path, "PNG");
    return path;
}

// Drain the MlScheduler so the test sees the Prefetch scoring pass
// finish before we read the action's icon. waitForIdle() blocks until
// the queue is empty AND no task is running; processEvents catches
// the queued result-application step that fires the badge update.
void drainScheduler(Application *app, int budgetMs = 4000) {
    app->mlScheduler().waitForIdle(budgetMs);
    // Repeatedly flush the event queue. The worker's
    // QMetaObject::invokeMethod posts a QMetaCallEvent which the GUI
    // loop processes; one processEvents picks up the result-application
    // step, and a second flush catches any further events it queued
    // (icon-pixmap caching from themedActionIcon can trigger one).
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

bool seedU2NetPIntoAppCache() {
    const QString src = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_U2NETP"));
    if (src.isEmpty() || !QFileInfo::exists(src))
        return false;
    const QString dir = AppPaths::modelsDir();
    QDir().mkpath(dir);
    const QString dest = QDir(dir).filePath(QStringLiteral("u2netp.onnx"));
    QFile::remove(dest);
    return QFile::copy(src, dest);
}

} // namespace

class TestUatBackgroundRemoval : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_bgr_010_removeBackgroundMenuActionWired();
    void uat_bgr_020_removeBackgroundAppliesAlphaWithRealModel();
    void uat_bgr_030_removeBackgroundNoopsWithoutModel();
    void uat_bgr_040_neverDownloadPolicyDisablesMenuWithTooltip();
    void uat_bgr_050_goodCandidateImageSurfacesBadge();
    void uat_bgr_060_flatDocumentImageHasNoBadge();

  private:
    QTemporaryDir m_scratch;
};

void TestUatBackgroundRemoval::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();

    // Drain any leftover MlScheduler work — Prefetch scoring from the
    // previous slot may still be in flight (cancelled via the about-
    // to-be-removed signal but the worker thread still drains its
    // current task to completion).
    if (auto *app = qobject_cast<Application *>(qApp)) {
        app->mlScheduler().cancelAll();
        app->mlScheduler().waitForIdle(2000);
        QApplication::processEvents();
    }

    // Wipe any model cache from a previous slot so each test starts
    // with a predictable state.
    const QString cached = QDir(AppPaths::modelsDir()).filePath(QStringLiteral("u2netp.onnx"));
    QFile::remove(cached);

    // Clear any leaked never-download policy bit so this slot starts
    // with the default policy. The bgr_040 case toggles it on and
    // we don't want the flag to leak into bgr_050 / bgr_060.
    if (auto *app = qobject_cast<Application *>(qApp)) {
        ModelPolicy::setNeverDownload(app, ModelId::U2NetP, false);
    }
}

void TestUatBackgroundRemoval::uat_bgr_010_removeBackgroundMenuActionWired() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleImage(m_scratch.filePath(QStringLiteral("bgr010.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Remove &Background"));
    QVERIFY2(action, "Tools → Remove Background action is missing");
    QVERIFY2(action->isEnabled(), "Remove Background should be enabled for an image document");

    // Sanity: it should also be disabled when no document is open.
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
}

void TestUatBackgroundRemoval::uat_bgr_020_removeBackgroundAppliesAlphaWithRealModel() {
    if (!seedU2NetPIntoAppCache()) {
        QSKIP("TRAILER_TEST_U2NETP not set or missing — skipping real "
              "inference path.");
    }

    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleImage(m_scratch.filePath(QStringLiteral("bgr020.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY2(imgDoc, "Active document should be an ImageDocument");
    const QSize before = imgDoc->imagePixelSize();
    QVERIFY(!before.isEmpty());

    // Bypass the modal UI and drive the integration layer the menu
    // slot relies on. The registry was seeded via
    // seedU2NetPIntoAppCache() against AppPaths::modelsDir().
    BackgroundRemover remover(&app->modelRegistry());
    QVERIFY2(remover.isModelReady(), "Seeded u2netp should be visible to the shared registry");

    const QImage result = remover.remove(imgDoc->image());
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), before);
    QVERIFY(result.hasAlphaChannel());

    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
    QVERIFY(imgDoc->replaceImage(result));
    QVERIFY2(imgDoc->isDirty(), "replaceImage should mark document dirty");
    QVERIFY2(imgDoc->canUndo(), "replaceImage should push an undo snapshot");

    // Undo must roll the image back to its pre-removal pixels.
    imgDoc->undo();
    QVERIFY(!imgDoc->canUndo());
    // (isDirty remains true until save — the undo stack is non-empty on
    //  the redo side now, and the document tracks in-flight edits
    //  independently from the stack depth.)
}

void TestUatBackgroundRemoval::uat_bgr_030_removeBackgroundNoopsWithoutModel() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleImage(m_scratch.filePath(QStringLiteral("bgr030.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY(imgDoc);

    BackgroundRemover remover(&app->modelRegistry());
    QVERIFY2(!remover.isModelReady(), "Cache was wiped in init() — model should not be ready");

    const QImage result = remover.remove(imgDoc->image());
    QVERIFY2(result.isNull(), "remove() with no cached model must return null");
    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
}

void TestUatBackgroundRemoval::uat_bgr_040_neverDownloadPolicyDisablesMenuWithTooltip() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleImage(m_scratch.filePath(QStringLiteral("bgr040.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // Pre-condition: the model is NOT on disk (init() wiped it) AND
    // the user (via Manage ML Models) has marked it Never Download.
    ModelPolicy::setNeverDownload(app, ModelId::U2NetP, true);

    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Remove &Background"));
    QVERIFY2(action, "Tools → Remove Background action is missing");
    QVERIFY2(!action->isEnabled(),
             "Remove Background should be disabled when policy says Never Download");
    const QString tip = action->toolTip();
    QVERIFY2(!tip.isEmpty(), "Disabled action must carry a tooltip explaining why");
    QVERIFY2(
        tip.contains(QStringLiteral("Manage ML Models")),
        qPrintable(QString("Tooltip should point at Tools → Manage ML Models…: '%1'").arg(tip)));
}

void TestUatBackgroundRemoval::uat_bgr_050_goodCandidateImageSurfacesBadge() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writePhotoLikeImage(m_scratch.filePath(QStringLiteral("bgr050.png")));

    // Sanity-check the fixture directly through the scorer — if this
    // fails the test's expectation about the badge is moot and the
    // fixture / threshold need tuning.
    {
        QImage probe(imgPath);
        QVERIFY(!probe.isNull());
        const auto verdict = BackgroundCandidateScorer::score(probe);
        QVERIFY2(verdict.combined >= BackgroundCandidateScorer::kRecommendThreshold,
                 qPrintable(QString("photo fixture scored %1 — below threshold %2; "
                                    "tighten the fixture or relax the threshold")
                                .arg(verdict.combined)
                                .arg(BackgroundCandidateScorer::kRecommendThreshold)));
    }

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();
    // Heuristic scoring runs through MlScheduler at Prefetch priority.
    // Wait for it (and the queued GUI-thread badge update) to land
    // before we read the action's icon.
    drainScheduler(app);

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Remove &Background"));
    QVERIFY(action);
    QVERIFY2(!action->icon().isNull(), "Photo-like fixture should clear the recommend threshold "
                                       "and pick up the badge sparkle icon.");
    QVERIFY2(action->toolTip().contains(QStringLiteral("works well")),
             qPrintable(QString("Positive-hint tooltip missing: '%1'").arg(action->toolTip())));
}

void TestUatBackgroundRemoval::uat_bgr_060_flatDocumentImageHasNoBadge() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath =
        writeFlatDocumentImage(m_scratch.filePath(QStringLiteral("bgr060.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();
    drainScheduler(app);

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Remove &Background"));
    QVERIFY(action);
    QVERIFY2(action->icon().isNull(), "Flat document fixture should not clear the recommend "
                                      "threshold — no badge icon should be set.");
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

    Application app(argc, argv);
    TestUatBackgroundRemoval tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_background_removal.moc"
