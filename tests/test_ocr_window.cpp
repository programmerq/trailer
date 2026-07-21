// Unit tests for the lazy per-page OCR window in trailer::OcrController
// (ADR 0013 §G13.3 / backlog 2026-07-15-lazy-per-page-ocr-window).
//
// The window logic lives in OcrController::onVisiblePageChanged: on every
// settle it OCR-submits the visible page (±kLazyWindowRadius neighbours as
// Prefetch) and cancels anything outside the window, so a jump recenters
// OCR on demand and never OCRs the whole document at once. These tests
// drive that method directly against a lightweight multi-page fake
// document and assert *which pages are enqueued* via the
// pendingPagesForTesting() seam (synchronous, execution-timing
// independent) plus the store's results for the CPU-discipline cases.
//
// A production multi-page selectable-text document that reaches the
// ambient path does not exist yet (images are single-page; PdfDocument
// reports hasTextLayer()==true and is skipped ahead of the window), so a
// fake IDocument is the right vehicle for the window mechanics — the same
// reason the SamController unit test hands the controller synthetic docs.
//
//   uat_ocr_win_010_windowIsVisiblePagePlusMinusTwo
//   uat_ocr_win_020_windowClampsAtDocumentEdges
//   uat_ocr_win_030_jumpRecentersAndCancelsOldWindow
//   uat_ocr_win_040_largeDocGetsAmbientWindowNotCancelToNothing
//   uat_ocr_win_050_backgroundOffSubmitsNothingExplicitStillRuns
//   uat_ocr_win_060_onBatteryOnlyVisiblePageRunsNeighboursSuppressed

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/SelectableTextStore.h"
#include "ml/CancellationToken.h"
#include "ml/OcrEngine.h"
#include "platform/PowerSource.h"
#include "settings/Settings.h"
#include "ui/OcrController.h"

#include <QImage>
#include <QPolygon>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace trailer;

namespace {

PowerState forceAc() { return PowerState::OnAC; }
PowerState forceBattery() { return PowerState::OnBattery; }

// A minimal multi-page document that advertises selectable text and NO
// coarse text layer, so onVisiblePageChanged runs its window logic. Only
// the members the OCR path touches are overridden; every other IDocument
// method keeps its default. createView is never called by these tests.
class FakeOcrDoc : public IDocument {
  public:
    explicit FakeOcrDoc(int pages) : m_pages(pages) {}

    QString displayName() const override { return QStringLiteral("fake-ocr-doc"); }
    QString filePath() const override { return {}; }
    QWidget *createView(QWidget *) override { return nullptr; }

    int pageCount() const override { return m_pages; }
    int currentPage() const override { return m_current; }
    void goToPage(int p) override { m_current = p; }

    bool supportsSelectableText() const override { return true; }
    bool hasTextLayer() const override { return false; }
    SelectableTextStore *selectableText() override { return &m_store; }

    QImage renderPageForOcr(int page) const override {
        // A tiny non-null raster, tinted per page so distinct pages hash
        // distinctly (hashImageContent folds in pixels + dims).
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(qRgb((page * 7 + 1) & 0xFF, (page * 13 + 3) & 0xFF, 17));
        return img;
    }

  private:
    int m_pages;
    int m_current = 0;
    SelectableTextStore m_store;
};

// Recognizer that returns exactly one block, so an executed page becomes
// hasResults()==true. Captures a shared counter by value so it stays alive
// past the controller if a worker is still draining. Thread-safe.
OcrController::RecognizeFn oneBlockRecognizer(std::shared_ptr<std::atomic<int>> calls) {
    return [calls](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
        if (calls)
            ++*calls;
        OcrEngine::TextBlock b;
        b.text = QStringLiteral("x");
        b.polygon = QPolygon(QRect(0, 0, 2, 2));
        return {b};
    };
}

void pumpUntil(int deadlineMs, const std::function<bool()> &done) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < deadlineMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (done())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

} // namespace

class TestOcrWindow : public QObject {
    Q_OBJECT
  public:
    Application *app = nullptr;

  private slots:
    void init();
    void cleanup();
    void uat_ocr_win_010_windowIsVisiblePagePlusMinusTwo();
    void uat_ocr_win_020_windowClampsAtDocumentEdges();
    void uat_ocr_win_030_jumpRecentersAndCancelsOldWindow();
    void uat_ocr_win_040_largeDocGetsAmbientWindowNotCancelToNothing();
    void uat_ocr_win_050_backgroundOffSubmitsNothingExplicitStillRuns();
    void uat_ocr_win_060_onBatteryOnlyVisiblePageRunsNeighboursSuppressed();

  private:
    void resetSettings() {
        app->settings().setMlRecognizeTextInBackground(true);
        app->settings().setMlRunOnBattery(false);
    }
};

void TestOcrWindow::init() {
    // Default to AC so speculative Prefetch runs (the battery test flips
    // this itself). cleanup() clears it after each test so the global
    // probe never leaks past this test class.
    PowerSource::setProbeForTesting(&forceAc);
    resetSettings();
}

void TestOcrWindow::cleanup() {
    // Symmetric teardown for init()'s probe install; matches the
    // test_ml_scheduler.cpp init()/cleanup() idiom so no global
    // PowerSource state leaks to later tests.
    PowerSource::clearProbeForTesting();
}

// Backlog Threshold 1 (Window). ADR G13.3 states N = 2 and asserts the
// window is exactly [k-2, k+2] with no page outside it.
void TestOcrWindow::uat_ocr_win_010_windowIsVisiblePagePlusMinusTwo() {
    QCOMPARE(OcrController::kLazyWindowRadius, 2);

    OcrController ctl(app);
    FakeOcrDoc doc(20);
    ctl.setDocument(&doc);
    ctl.setModelReadyForTesting(true);
    auto calls = std::make_shared<std::atomic<int>>(0);
    ctl.setRecognizerForTesting(oneBlockRecognizer(calls));

    ctl.onVisiblePageChanged(10);
    // Synchronous, execution-timing independent: exactly [8..12] pending.
    const std::vector<int> expected{8, 9, 10, 11, 12};
    QCOMPARE(ctl.pendingPagesForTesting(), expected);

    // And the whole window actually recognises (execution check); nothing
    // outside it ever does.
    auto *store = doc.selectableText();
    pumpUntil(4000, [store]() {
        for (int p = 8; p <= 12; ++p)
            if (!store->hasResults(p))
                return false;
        return true;
    });
    for (int p = 8; p <= 12; ++p)
        QVERIFY2(store->hasResults(p), qPrintable(QStringLiteral("page %1 in window").arg(p)));
    QVERIFY2(!store->hasResults(7), "page below window must not OCR");
    QVERIFY2(!store->hasResults(13), "page above window must not OCR");
}

// Backlog Threshold 1 edge behaviour: the window clamps to the document,
// it never submits negative or past-end pages.
void TestOcrWindow::uat_ocr_win_020_windowClampsAtDocumentEdges() {
    OcrController ctl(app);
    FakeOcrDoc doc(6); // pages 0..5
    ctl.setDocument(&doc);
    ctl.setModelReadyForTesting(true);
    auto calls = std::make_shared<std::atomic<int>>(0);
    ctl.setRecognizerForTesting(oneBlockRecognizer(calls));

    ctl.onVisiblePageChanged(0);
    QCOMPARE(ctl.pendingPagesForTesting(), (std::vector<int>{0, 1, 2}));

    // Jump to the last page: clamps to [3, 4, 5].
    ctl.onVisiblePageChanged(5);
    QCOMPARE(ctl.pendingPagesForTesting(), (std::vector<int>{3, 4, 5}));
}

// Backlog Threshold 2 (Jump). Jumping to a far page recenters OCR on the
// new window and cancels the prior speculative pages (on-demand, not
// greedy). Asserted synchronously so no old-window page could have raced
// to completion.
void TestOcrWindow::uat_ocr_win_030_jumpRecentersAndCancelsOldWindow() {
    OcrController ctl(app);
    FakeOcrDoc doc(30);
    ctl.setDocument(&doc);
    ctl.setModelReadyForTesting(true);
    auto calls = std::make_shared<std::atomic<int>>(0);
    ctl.setRecognizerForTesting(oneBlockRecognizer(calls));

    ctl.onVisiblePageChanged(5);
    QCOMPARE(ctl.pendingPagesForTesting(), (std::vector<int>{3, 4, 5, 6, 7}));

    // Far jump — no window overlap.
    ctl.onVisiblePageChanged(20);
    const std::vector<int> after = ctl.pendingPagesForTesting();
    QCOMPARE(after, (std::vector<int>{18, 19, 20, 21, 22}));
    // None of the old window's pages remain pending — they were cancelled.
    for (int oldPage : {3, 4, 5, 6, 7})
        QVERIFY2(std::find(after.begin(), after.end(), oldPage) == after.end(),
                 qPrintable(QStringLiteral("old page %1 must be cancelled").arg(oldPage)));
}

// Backlog context: the prior behaviour cancelled a large doc (>50 pages)
// down to nothing on the ambient path. It must now get the same lazy
// window. Proves the cancel-to-nothing early return is gone.
void TestOcrWindow::uat_ocr_win_040_largeDocGetsAmbientWindowNotCancelToNothing() {
    OcrController ctl(app);
    FakeOcrDoc doc(120); // > kLargeDocPageThreshold (50)
    ctl.setDocument(&doc);
    QVERIFY2(ctl.isLargeDoc(), "120-page doc must count as large");
    ctl.setModelReadyForTesting(true);
    auto calls = std::make_shared<std::atomic<int>>(0);
    ctl.setRecognizerForTesting(oneBlockRecognizer(calls));

    ctl.onVisiblePageChanged(60);
    // Previously: empty (cancel-to-nothing). Now: the full ±2 window.
    QCOMPARE(ctl.pendingPagesForTesting(), (std::vector<int>{58, 59, 60, 61, 62}));
}

// Backlog Threshold 4a (CPU discipline). With recognize_text_in_background
// off there are zero ambient submissions; the explicit Recognize path is
// unaffected.
void TestOcrWindow::uat_ocr_win_050_backgroundOffSubmitsNothingExplicitStillRuns() {
    OcrController ctl(app);
    FakeOcrDoc doc(20);
    ctl.setDocument(&doc);
    ctl.setModelReadyForTesting(true);
    ctl.setProgressRevealDelayMs(0);
    auto calls = std::make_shared<std::atomic<int>>(0);
    ctl.setRecognizerForTesting(oneBlockRecognizer(calls));

    app->settings().setMlRecognizeTextInBackground(false);
    ctl.onVisiblePageChanged(10);
    QVERIFY2(ctl.pendingPagesForTesting().empty(),
             "background OCR off must submit zero ambient pages");

    // Explicit Recognize Text (UserAction) still runs regardless.
    auto *store = doc.selectableText();
    ctl.submitUserPages(&doc, {10}, /*forceRerun=*/false);
    pumpUntil(4000, [store]() { return store->hasResults(10); });
    QVERIFY2(store->hasResults(10), "explicit Recognize must run with background OCR off");
    QVERIFY(calls->load() >= 1);
}

// Backlog Threshold 4b (CPU discipline). On battery with run_on_battery
// off, the scheduler pre-cancels the speculative Prefetch neighbours; only
// the VisiblePage submission (the visible page) recognises.
void TestOcrWindow::uat_ocr_win_060_onBatteryOnlyVisiblePageRunsNeighboursSuppressed() {
    PowerSource::setProbeForTesting(&forceBattery);
    app->settings().setMlRunOnBattery(false);
    app->mlScheduler().reevaluatePowerPolicy();

    OcrController ctl(app);
    FakeOcrDoc doc(20);
    ctl.setDocument(&doc);
    ctl.setModelReadyForTesting(true);
    auto calls = std::make_shared<std::atomic<int>>(0);
    ctl.setRecognizerForTesting(oneBlockRecognizer(calls));

    auto *store = doc.selectableText();
    ctl.onVisiblePageChanged(10);
    // The visible page must recognise.
    pumpUntil(4000, [store]() { return store->hasResults(10); });
    QVERIFY2(store->hasResults(10), "visible page (VisiblePage) must run on battery");

    // Give any (pre-cancelled) neighbours ample time — they must NOT run.
    pumpUntil(400, []() { return false; });
    for (int p : {8, 9, 11, 12})
        QVERIFY2(!store->hasResults(p),
                 qPrintable(QStringLiteral("neighbour page %1 (Prefetch) must be "
                                           "battery-suppressed")
                                .arg(p)));

    PowerSource::setProbeForTesting(&forceAc);
}

// Hand-rolled main: exactly one trailer::Application (a QApplication
// subclass) for the whole binary, mirroring test_sam_controller.
int main(int argc, char *argv[]) {
    QTemporaryDir home;
    if (!home.isValid())
        return 1;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
    QDir().mkpath(home.path() + "/.config/trailer");
    QDir().mkpath(home.path() + "/.local/share/trailer");

    trailer::Application app(argc, argv);
    TestOcrWindow tests;
    tests.app = &app;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_ocr_window.moc"
