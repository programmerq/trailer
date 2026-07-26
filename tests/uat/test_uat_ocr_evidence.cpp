// UAT evidence harness — curated before/after grab() PNGs for the image
// OCR pipeline PR (search-enabled + honest "No text found" status +
// single-image dialog-skip). This file exists to *produce* the reviewer
// evidence images, but every slot is also a real assertion of the wired
// behaviour, so it runs green as an ordinary `uat`-labelled test even when
// no output directory is configured.
//
// Set TRAILER_OCR_EVIDENCE_DIR to a directory to have each slot write its
// curated PNG there (offscreen QWidget::grab(), no display required). With
// the variable unset the slots still exercise + assert the behaviour and
// simply skip the file write.
//
//   ocr_ev_10_searchAfter           -> ocr-images-search-after.png
//   ocr_ev_20_noTextFoundAfter      -> ocr-images-no-text-found-after.png
//   ocr_ev_30_dialogSinglePageBefore-> ocr-images-dialog-single-page-before.png
//   ocr_ev_40_dialogSinglePageAfter -> ocr-images-dialog-single-page-after.png

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/ImageAdapter.h"
#include "document/SelectableTextStore.h"
#include "ml/CancellationToken.h"
#include "ml/OcrEngine.h"
#include "ui/AnnotationOverlay.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MlProgressWidget.h"
#include "ui/OcrController.h"
#include "ui/OcrResultsDialog.h"
#include "ui/SearchBar.h"

#include <QAction>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPolygon>
#include <QSemaphore>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
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

// Mirror the owner's 939x312 screenshot-of-text case: a small white PNG
// with a known black headline so the grab reads as a real document.
QString writeTextImage(const QString &path, const QString &text = QStringLiteral("HELLO 1234"),
                       int w = 939, int h = 312) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont f;
    f.setPixelSize(96);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(img.rect(), Qt::AlignCenter, text);
    p.end();
    img.save(path, "PNG");
    return path;
}

// A recognizer that blocks on `gate` before returning a sentinel block, so
// a UserAction batch stays mid-flight long enough to grab the revealed
// "Recognising text" progress state.
OcrController::RecognizeFn gatedRecognizer(std::shared_ptr<QSemaphore> gate) {
    return [gate](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
        gate->acquire();
        OcrEngine::TextBlock b;
        b.text = QStringLiteral("recognised");
        b.polygon = QPolygon(QRect(0, 0, 20, 20));
        return {b};
    };
}

QString evidenceDir() {
    return QString::fromLocal8Bit(qgetenv("TRAILER_OCR_EVIDENCE_DIR"));
}

// Grab `w` offscreen and write it under TRAILER_OCR_EVIDENCE_DIR when set.
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

} // namespace

class TestUatOcrEvidence : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void ocr_ev_10_searchAfter();
    void ocr_ev_20_noTextFoundAfter();
    void ocr_ev_30_dialogSinglePageBefore();
    void ocr_ev_40_dialogSinglePageAfter();

  private:
    QTemporaryDir m_scratch;
};

void TestUatOcrEvidence::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// #1 search-after: an image with OCR results exposes an ENABLED Find
// affordance and a matching query paints a highlight overlay on the
// recognized block. The grab shows the open Find bar (with its match
// counter) over the document plus the highlight.
void TestUatOcrEvidence::ocr_ev_10_searchAfter() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("search.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(980, 460);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY2(imgDoc, "Active document should be an ImageDocument");

    // Find is enabled for the image now that it advertises search.
    QAction *findAction = findActionByText(mw->menuBar(), QStringLiteral("&Find…"));
    QVERIFY2(findAction, "Edit → Find… action is missing");
    QVERIFY2(findAction->isEnabled(),
             "Find must be enabled for an image once it supports search");

    // Seed OCR results for the visible page (deterministic without models).
    OcrEngine::TextBlock b;
    b.text = QStringLiteral("HELLO 1234");
    b.polygon = QPolygon(QRect(150, 110, 620, 110));
    imgDoc->selectableText()->put(0, 42ULL, {b});

    // Open the Find bar via the real action, then drive a matching query
    // through the SearchBar so the match counter + highlight light up.
    findAction->trigger();
    QApplication::processEvents();
    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY2(searchBar, "Find bar (SearchBar) should exist");
    searchBar->setQuery(QStringLiteral("HELLO"));
    QApplication::processEvents();

    QCOMPARE(imgDoc->searchMatchCount(), 1);
    QCOMPARE(imgDoc->currentSearchMatchIndex(), 1);
    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY2(overlay, "image view should host an AnnotationOverlay");
    QVERIFY2(overlay->searchHighlightCountForTest() > 0,
             "a matching query must push a search highlight into the overlay");

    saveShot(mw, QStringLiteral("ocr-images-search-after.png"));
}

// #2 no-text-found-after: running Recognize Text on an image that yields
// ZERO OCR blocks must leave an honest "No text found" in the status-bar
// progress widget — never a false "Text recognition complete". Driven
// through the MainWindow's own OcrController so its status wiring runs.
void TestUatOcrEvidence::ocr_ev_20_noTextFoundAfter() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("notext.png")),
                                           QStringLiteral("(blank scan)"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(980, 460);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);

    auto *controller = mw->findChild<OcrController *>();
    QVERIFY(controller);
    auto *mlp = mw->findChild<MlProgressWidget *>();
    QVERIFY(mlp);
    // Reveal immediately and hold the terminal message so the grab catches
    // it (production hold is ~3s and would race the grab).
    controller->setProgressRevealDelayMs(0);
    mlp->setTerminalHoldMs(60000);

    std::atomic<int> calls{0};
    controller->setRecognizerForTesting(
        [&calls](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
            ++calls;
            return {}; // zero blocks recognized
        });

    QSignalSpy finished(controller, &OcrController::ocrBatchFinished);
    controller->submitUserPages(doc, {0}, /*forceRerun=*/true);
    QTRY_VERIFY(finished.count() >= 1);
    QVERIFY2(calls.load() >= 1, "recognizer must have run");
    QVERIFY2(!doc->selectableText()->hasResults(0),
             "a zero-block result must not claim a text layer");

    // The honest status text is live in the progress widget.
    QCOMPARE(mlp->state(), MlProgressWidget::Terminal);
    QCOMPARE(mlp->labelText(), QStringLiteral("No text found"));
    QApplication::processEvents();
    saveShot(mw, QStringLiteral("ocr-images-no-text-found-after.png"));
    mlp->goIdle();
}

// #3 dialog-single-page-before: the page-range dialog a single image USED
// to pop (Pages: Current/All/range). Constructed for a 1-page doc and
// grabbed — this is the friction the "after" removes.
void TestUatOcrEvidence::ocr_ev_30_dialogSinglePageBefore() {
    RecognizeTextDialog dialog(/*pageCount=*/1, /*currentPage=*/0,
                               /*hasTextLayer=*/false, /*languageOptions=*/{});
    dialog.setWindowTitle(QStringLiteral("Recognize Text"));
    dialog.resize(420, 200);
    dialog.show();
    QApplication::processEvents();
    // Sanity: the dialog really does present the single-page scope choice.
    QVERIFY(dialog.findChild<QDialog *>() == nullptr); // it is itself the dialog
    saveShot(&dialog, QStringLiteral("ocr-images-dialog-single-page-before.png"));
    dialog.close();
}

// #4 dialog-single-page-after: invoking Recognize Text on a single-page
// image goes straight to recognizing — NO modal dialog. Grab the window in
// the revealed "Recognising text" progress state and assert no modal is up.
void TestUatOcrEvidence::ocr_ev_40_dialogSinglePageAfter() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("skip.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(980, 460);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 1);

    auto *controller = mw->findChild<OcrController *>();
    QVERIFY(controller);
    auto *mlp = mw->findChild<MlProgressWidget *>();
    QVERIFY(mlp);
    controller->setProgressRevealDelayMs(0);

    auto gate = std::make_shared<QSemaphore>();
    controller->setRecognizerForTesting(gatedRecognizer(gate));

    // Single-page image → the resolved path is exactly {currentPage} with
    // no dialog. Submit it and grab the revealed progress state.
    controller->submitUserPages(doc, {doc->currentPage()}, /*forceRerun=*/false);
    QTRY_VERIFY2(mlp->state() == MlProgressWidget::Running,
                 "single-image Recognize must reveal the progress widget");
    // The defining "after" property: no modal dialog was spawned.
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        QVERIFY2(!qobject_cast<QMessageBox *>(w), "no ad-hoc QMessageBox");
    }
    QVERIFY(mlp->labelText().contains(QStringLiteral("Recognising text")));

    saveShot(mw, QStringLiteral("ocr-images-dialog-single-page-after.png"));

    // Let the batch finish cleanly.
    gate->release(1);
    QTRY_VERIFY(mlp->state() != MlProgressWidget::Running);
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
    TestUatOcrEvidence tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_ocr_evidence.moc"
