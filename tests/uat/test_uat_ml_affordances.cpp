// UAT harness — ML status-bar affordances (ADR 0002).
//
// Covers the six declared gates for the progress / cancel / missing-model
// work. Everything is deterministic and offscreen: OCR is driven through a
// test recogniser seam (OcrController::setRecognizerForTesting) so the
// batch machinery runs without real ONNX models, and page timing is
// controlled by a QSemaphore rather than wall-clock sleeps. The reveal
// delay and terminal-hold thresholds are settable so G2's "reveal / no
// reveal" split needs no real time to pass.
//
//   uat_ml_g1_determinateBatchProgress
//   uat_ml_g2_revealDelayHonoursThreshold
//   uat_ml_g3_cancelDiscardsInFlightKeepsCompleted
//   uat_ml_g4_cancelPresentAndKeyScoped
//   uat_ml_g5_autoOcrMissingModelShowsInContextHint
//   uat_ml_g6_explicitMenuTooltipUsesBenefitLanguage

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/SelectableTextStore.h"
#include "ml/CancellationToken.h"
#include "ml/MlScheduler.h"
#include "ml/ModelRegistry.h"
#include "ml/OcrEngine.h"
#include "settings/AppPaths.h"
#include "settings/Settings.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MlProgressWidget.h"
#include "ui/ModelManagerDialog.h"
#include "ui/OcrController.h"

#include <QAction>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPainter>
#include <QPolygon>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QtTest>

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

void saveEvidence(QWidget *w, const QString &fileName) {
    const QString dir = qEnvironmentVariable("TRAILER_UAT_EVIDENCE_DIR");
    if (dir.isEmpty() || !w)
        return;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(fileName);
    if (!w->grab().save(path, "PNG"))
        qWarning("ml_affordances: failed to save evidence %s", qPrintable(path));
}

QString writeSamplePdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                   QStringLiteral("Page %1").arg(i + 1));
        if (i < pages - 1)
            writer.newPage();
    }
    p.end();
    return path;
}

QString writeTextImage(const QString &path) {
    QImage img(640, 200, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    QFont f;
    f.setPixelSize(80);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("HELLO 1234"));
    p.end();
    img.save(path, "PNG");
    return path;
}

void wipePpOcrCache() {
    const QString dir = AppPaths::modelsDir();
    QFile::remove(QDir(dir).filePath(QStringLiteral("pp_ocr_det.onnx")));
    QFile::remove(QDir(dir).filePath(QStringLiteral("pp_ocr_rec_en.onnx")));
}

// A recogniser seam whose per-page completion is gated on a semaphore, so
// the test can hold pages mid-flight and release them one at a time. It
// cooperates with cancellation: a blocked page exits when its token flips.
OcrController::RecognizeFn gatedRecognizer(std::shared_ptr<QSemaphore> gate) {
    return [gate](const QImage &, const CancellationToken *token)
               -> QVector<OcrEngine::TextBlock> {
        for (;;) {
            if (token && token->isCancelled())
                break;
            if (gate->tryAcquire(1, 5))
                break;
        }
        OcrEngine::TextBlock block;
        block.text = QStringLiteral("x");
        block.confidence = 1.0f;
        block.polygon = QPolygon(QRect(0, 0, 10, 10));
        QVector<OcrEngine::TextBlock> out;
        out.append(block);
        return out;
    };
}

// Mirror of MainWindow's OcrController→MlProgressWidget wiring, used by the
// controller-level gates (G1–G3) so the exact reveal / progress / terminal
// mapping is exercised without standing up the whole window.
struct ProgressBinder {
    MlProgressWidget *widget = nullptr;
    int total = 0;
    int completed = 0;
    bool revealed = false;

    void bind(OcrController *c) {
        QObject::connect(c, &OcrController::ocrBatchStarted, widget, [this](int t) {
            total = t;
            completed = 0;
            revealed = false;
        });
        QObject::connect(c, &OcrController::ocrBatchProgress, widget, [this](int done, int t) {
            total = t;
            completed = done;
            if (revealed)
                widget->setProgress(done);
        });
        QObject::connect(c, &OcrController::ocrBatchShouldReveal, widget, [this]() {
            revealed = true;
            if (total >= 2) {
                widget->beginDeterminate(QStringLiteral("Recognising text"), total);
                widget->setProgress(completed);
            } else {
                widget->beginIndeterminate(QStringLiteral("Recognising text"));
            }
        });
        QObject::connect(c, &OcrController::ocrBatchFinished, widget, [this](bool cancelled) {
            if (revealed) {
                widget->finishWithMessage(
                    cancelled ? QStringLiteral("Text recognition cancelled — no changes saved")
                              : QStringLiteral("Text recognition complete"));
            }
            revealed = false;
        });
        QObject::connect(widget, &MlProgressWidget::cancelRequested, c,
                         &OcrController::cancelActiveBatch);
    }
};

} // namespace

class TestUatMlAffordances : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void cleanup();

    void uat_ml_g1_determinateBatchProgress();
    void uat_ml_g2_revealDelayHonoursThreshold();
    void uat_ml_g3_cancelDiscardsInFlightKeepsCompleted();
    void uat_ml_g4_cancelPresentAndKeyScoped();
    void uat_ml_g5_autoOcrMissingModelShowsInContextHint();
    void uat_ml_g6_explicitMenuTooltipUsesBenefitLanguage();

  private:
    QTemporaryDir m_scratch;
};

void TestUatMlAffordances::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    wipePpOcrCache();
    if (auto *app = qobject_cast<Application *>(qApp)) {
        // Reset any leaked never-download policy so each slot starts clean.
        ModelPolicy::setNeverDownload(app, ModelId::PpOcrDetector, false);
        ModelPolicy::setNeverDownload(app, ModelId::PpOcrRecognizerLatin, false);
        app->settings().setMlRecognizeTextInBackground(true);
    }
}

void TestUatMlAffordances::cleanup() {
    if (auto *app = qobject_cast<Application *>(qApp))
        app->mlScheduler().waitForIdle(3000);
    QApplication::processEvents();
}

// G1 — a batch of N>=2 pages drives a determinate widget whose counter
// reaches N monotonically.
void TestUatMlAffordances::uat_ml_g1_determinateBatchProgress() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeSamplePdf(m_scratch.filePath(QStringLiteral("g1.pdf")), 4);
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 4);

    OcrController controller(app);
    controller.setDocument(doc);
    controller.setProgressRevealDelayMs(0);
    auto gate = std::make_shared<QSemaphore>();
    controller.setRecognizerForTesting(gatedRecognizer(gate));

    MlProgressWidget widget;
    ProgressBinder binder;
    binder.widget = &widget;
    binder.bind(&controller);

    QSignalSpy started(&controller, &OcrController::ocrBatchStarted);
    QSignalSpy progress(&controller, &OcrController::ocrBatchProgress);
    QSignalSpy finished(&controller, &OcrController::ocrBatchFinished);

    controller.submitUserPages(doc, {0, 1, 2, 3}, /*forceRerun=*/false);
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.at(0).at(0).toInt(), 4);

    // Let the first two pages through, hold the rest.
    gate->release(2);
    QTRY_VERIFY(widget.isDeterminate() && widget.value() == 2);
    QCOMPARE(widget.total(), 4);
    QVERIFY(widget.cancelVisible());
    QVERIFY(widget.labelText().contains(QStringLiteral("2 / 4")));
    saveEvidence(&widget, QStringLiteral("ml_g1_running_2of4.png"));

    // Release the remainder and reach N.
    gate->release(2);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toBool(), false);
    QTRY_COMPARE(widget.value(), 4);

    // Progress was monotonically non-decreasing and terminated at 4.
    QCOMPARE(progress.count(), 4);
    int last = 0;
    for (const auto &row : progress) {
        const int done = row.at(0).toInt();
        QVERIFY(done >= last);
        last = done;
    }
    QCOMPARE(last, 4);
}

// G2 — the reveal delay is honoured: with delay 0 an in-flight batch
// reveals the widget; with a large delay a batch that finishes first never
// reveals it.
void TestUatMlAffordances::uat_ml_g2_revealDelayHonoursThreshold() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeSamplePdf(m_scratch.filePath(QStringLiteral("g2.pdf")), 2);
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();
    IDocument *doc = currentMainWindow()->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);

    // --- Part A: delay 0, batch still running -> reveal happens. ---
    {
        OcrController controller(app);
        controller.setDocument(doc);
        controller.setProgressRevealDelayMs(0);
        auto gate = std::make_shared<QSemaphore>();
        controller.setRecognizerForTesting(gatedRecognizer(gate));
        MlProgressWidget widget;
        ProgressBinder binder;
        binder.widget = &widget;
        binder.bind(&controller);
        QSignalSpy reveal(&controller, &OcrController::ocrBatchShouldReveal);

        controller.submitUserPages(doc, {0, 1}, /*forceRerun=*/true);
        QTRY_COMPARE(reveal.count(), 1);
        QVERIFY(binder.revealed);
        QCOMPARE(widget.state(), MlProgressWidget::Running);
        saveEvidence(&widget, QStringLiteral("ml_g2_revealed.png"));
        gate->release(2); // let it drain
        app->mlScheduler().waitForIdle(3000);
        QApplication::processEvents();
    }

    // --- Part B: huge delay, instant batch -> never reveals. ---
    {
        OcrController controller(app);
        controller.setDocument(doc);
        controller.setProgressRevealDelayMs(100000);
        // Ungated recogniser: returns immediately.
        controller.setRecognizerForTesting(
            [](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
                OcrEngine::TextBlock b;
                b.text = QStringLiteral("x");
                b.polygon = QPolygon(QRect(0, 0, 4, 4));
                QVector<OcrEngine::TextBlock> v;
                v.append(b);
                return v;
            });
        MlProgressWidget widget;
        ProgressBinder binder;
        binder.widget = &widget;
        binder.bind(&controller);
        QSignalSpy reveal(&controller, &OcrController::ocrBatchShouldReveal);
        QSignalSpy finished(&controller, &OcrController::ocrBatchFinished);

        controller.submitUserPages(doc, {0, 1}, /*forceRerun=*/true);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(reveal.count(), 0);
        QCOMPARE(widget.state(), MlProgressWidget::Idle);
        saveEvidence(&widget, QStringLiteral("ml_g2_idle_no_reveal.png"));
    }
}

// G3 — cancel mid-run keeps completed pages, discards in-flight and
// not-started pages (no half-recognised page), and shows the terminal
// "cancelled" message before returning to idle.
void TestUatMlAffordances::uat_ml_g3_cancelDiscardsInFlightKeepsCompleted() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeSamplePdf(m_scratch.filePath(QStringLiteral("g3.pdf")), 4);
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();
    IDocument *doc = currentMainWindow()->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    SelectableTextStore *store = doc->selectableText();
    QVERIFY(store);

    OcrController controller(app);
    controller.setDocument(doc);
    controller.setProgressRevealDelayMs(0);
    auto gate = std::make_shared<QSemaphore>();
    controller.setRecognizerForTesting(gatedRecognizer(gate));

    MlProgressWidget widget;
    widget.setTerminalHoldMs(10);
    ProgressBinder binder;
    binder.widget = &widget;
    binder.bind(&controller);

    controller.submitUserPages(doc, {0, 1, 2, 3}, /*forceRerun=*/true);

    // Complete pages 0 and 1; page 2 is now in flight (blocked), page 3
    // queued.
    gate->release(2);
    QTRY_VERIFY(store->hasResults(0) && store->hasResults(1));
    QTRY_VERIFY(binder.revealed);

    controller.cancelActiveBatch();

    // Give any late apply for the in-flight page a chance to run; the
    // cancellation guard must discard it.
    QApplication::processEvents();
    QApplication::processEvents();

    QVERIFY2(store->hasResults(0), "page 0 completed before cancel must keep its text");
    QVERIFY2(store->hasResults(1), "page 1 completed before cancel must keep its text");
    QVERIFY2(!store->hasResults(2), "in-flight page 2 must not persist a half-recognised result");
    QVERIFY2(!store->hasResults(3), "not-started page 3 must have no text");

    QCOMPARE(widget.state(), MlProgressWidget::Terminal);
    QVERIFY(widget.labelText().contains(QStringLiteral("cancelled")));
    QVERIFY(widget.labelText().contains(QStringLiteral("no changes saved")));
    saveEvidence(&widget, QStringLiteral("ml_g3_cancelled.png"));

    // Terminal message then idle.
    QTRY_COMPARE(widget.state(), MlProgressWidget::Idle);
}

// G4 — while a foreground op runs the ✕ is present and the ⌘. action is
// enabled; when no user batch runs the action is disabled; Esc is not
// bound to ML cancel.
void TestUatMlAffordances::uat_ml_g4_cancelPresentAndKeyScoped() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeSamplePdf(m_scratch.filePath(QStringLiteral("g4.pdf")), 4);
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);

    auto *controller = mw->findChild<OcrController *>();
    QVERIFY(controller);
    auto *progress = mw->findChild<MlProgressWidget *>();
    QVERIFY(progress);

    // The scoped keyboard cancel action.
    QAction *cancelAction = nullptr;
    for (QAction *a : mw->findChildren<QAction *>()) {
        if (a->text() == QStringLiteral("Cancel ML Operation")) {
            cancelAction = a;
            break;
        }
    }
    QVERIFY2(cancelAction, "⌘. cancel action must exist");
    QCOMPARE(cancelAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Period));
    QVERIFY2(cancelAction->shortcut() != QKeySequence(Qt::Key_Escape),
             "bare Escape must NOT be bound to ML cancel");
    // No user batch yet -> the scoped key is disabled (a ⌘. press cannot
    // cancel ambient auto-OCR).
    QVERIFY2(!cancelAction->isEnabled(), "cancel action must be disabled with no foreground op");

    controller->setProgressRevealDelayMs(0);
    auto gate = std::make_shared<QSemaphore>();
    controller->setRecognizerForTesting(gatedRecognizer(gate));

    controller->submitUserPages(doc, {0, 1, 2, 3}, /*forceRerun=*/true);
    // ocrBatchStarted fires synchronously -> the scoped key is now live.
    QVERIFY2(cancelAction->isEnabled(), "cancel action must enable while a batch runs");
    QTRY_VERIFY(progress->cancelVisible());
    QVERIFY(progress->state() == MlProgressWidget::Running);
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    saveEvidence(mw, QStringLiteral("ml_g4_running_with_cancel.png"));

    // Trigger the scoped cancel; the action disables again afterwards.
    cancelAction->trigger();
    gate->release(4); // unblock any in-flight worker so it can exit
    QTRY_VERIFY(!cancelAction->isEnabled());
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
}

// G5 — auto-OCR on a supported doc with the model absent surfaces the
// non-modal in-context hint (no dialog), and hides it again once the model
// is present.
void TestUatMlAffordances::uat_ml_g5_autoOcrMissingModelShowsInContextHint() {
    QVERIFY(m_scratch.isValid());
    const QString img = writeTextImage(m_scratch.filePath(QStringLiteral("g5.png")));
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({img});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QVERIFY(doc->supportsSelectableText());
    QVERIFY(!doc->hasTextLayer());

    auto *controller = mw->findChild<OcrController *>();
    QVERIFY(controller);
    auto *hint = mw->findChild<QLabel *>(QStringLiteral("ocrModelMissingHint"));
    QVERIFY2(hint, "missing-model in-context hint widget must exist");

    // Force "model absent" and re-derive.
    controller->setModelReadyForTesting(false);
    controller->onVisiblePageChanged(doc->currentPage());
    QApplication::processEvents();

    QVERIFY2(hint->isVisible(), "hint must show when auto-OCR would run but the model is absent");
    // No modal was spawned to explain unavailability.
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        QVERIFY2(!qobject_cast<QMessageBox *>(w), "no QMessageBox may be spawned");
        QVERIFY2(!qobject_cast<QDialog *>(w), "no QDialog may be spawned");
    }
    saveEvidence(mw, QStringLiteral("ml_g5_missing_model_hint.png"));

    // Once the model is present the hint hides again (state-driven).
    controller->setModelReadyForTesting(true);
    controller->onVisiblePageChanged(doc->currentPage());
    QApplication::processEvents();
    QVERIFY2(!hint->isVisible(), "hint must hide once the model is present");
}

// G6 — with the OCR models blocked/absent the explicit menu item is
// disabled and its tooltip uses benefit language (no "model" jargon token)
// while still naming the download path.
void TestUatMlAffordances::uat_ml_g6_explicitMenuTooltipUsesBenefitLanguage() {
    QVERIFY(m_scratch.isValid());
    const QString img = writeTextImage(m_scratch.filePath(QStringLiteral("g6.png")));
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // Model absent (wiped in init) AND policy = never download -> disabled.
    ModelPolicy::setNeverDownload(app, ModelId::PpOcrDetector, true);
    ModelPolicy::setNeverDownload(app, ModelId::PpOcrRecognizerLatin, true);

    app->openFiles({img});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Reco&gnize Text…"));
    QVERIFY2(action, "Recognize Text action missing");
    QVERIFY2(!action->isEnabled(),
             "Recognize Text must be disabled when the language download is blocked");
    const QString tip = action->toolTip();
    QVERIFY2(!tip.isEmpty(), "disabled action must carry a tooltip");
    QVERIFY2(!tip.contains(QStringLiteral("model")),
             qPrintable(QString("tooltip must avoid the 'model' jargon token: '%1'").arg(tip)));
    QVERIFY2(tip.contains(QStringLiteral("language")),
             qPrintable(QString("tooltip should use benefit language: '%1'").arg(tip)));
    QVERIFY2(tip.contains(QStringLiteral("Manage ML Models")),
             qPrintable(QString("tooltip should name the download path: '%1'").arg(tip)));
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
    TestUatMlAffordances tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_ml_affordances.moc"
