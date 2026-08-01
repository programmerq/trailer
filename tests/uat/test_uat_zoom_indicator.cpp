// UAT harness — Zoom-% status indicator
//
// Drives Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen to exercise the status-bar zoom indicator
// (the permanent QLabel objectName "zoomIndicator"):
//   - Opening a zoomable image shows the label and reads the current
//     zoom as an integer percent (== qRound(zoomFactor * 100)).
//   - With no document open the label is hidden (nothing to read out).
//   - Zooming in updates the readout to the new percent.
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a
// throwaway sandbox and every case starts from a no-window baseline.

#include "app/Application.h"
#include "document/IDocument.h"
#include "platform/ReducedMotion.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QSettings>
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

// Pump the event loop in short slices until `pred` holds or `budgetMs`
// elapses, then return pred()'s final value. Deterministic wait for the
// fade timer/animation to complete instead of a fixed sleep.
template <typename Pred> bool pumpUntil(Pred pred, int budgetMs = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < budgetMs)
        QTest::qWait(10);
    return pred();
}

QLabel *zoomIndicator(MainWindow *mw) {
    return mw->findChild<QLabel *>(QStringLiteral("zoomIndicator"));
}

// Find one of MainWindow's zoom QActions by its (mnemonic-carrying)
// visible text — the actions have no objectName, so match on text().
QAction *actionByText(MainWindow *mw, const QString &text) {
    const auto actions = mw->findChildren<QAction *>();
    for (QAction *a : actions) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

// A plain, static RGB image — supportsZoom() is true for these
// (non-animated, non-null), so opening one surfaces the indicator.
QString writeStaticImage(const QString &path) {
    QImage img(200, 150, QImage::Format_ARGB32);
    img.fill(qRgb(200, 210, 220));
    img.save(path, "PNG");
    return path;
}

// The integer-percent string the indicator is expected to show for a
// given document, matching updateZoomIndicator()'s own formatting.
QString expectedPercent(IDocument *doc) {
    return QStringLiteral("%1%").arg(qRound(doc->zoomFactor() * 100.0));
}

} // namespace

class TestUatZoomIndicator : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_zoom_ind_010_actualSizeRevealsCorrectPercent();
    void uat_zoom_ind_020_noDocumentHidesIndicator();
    void uat_zoom_ind_030_zoomInUpdatesPercent();
    void uat_zoom_ind_040_explicitZoomActionRevealsAndFades();    // UAT-VWR-103
    void uat_zoom_ind_050_docOpenAndSwitchStaySilent();            // UAT-VWR-104
    void uat_zoom_ind_060_reduceMotionSkipsFade();                 // UAT-VWR-105
    void uat_zoom_ind_070_togglingReadoutDoesNotShiftSiblingWidgets(); // UAT-VWR-106 (G10)

  private:
    QTemporaryDir m_scratch;
};

void TestUatZoomIndicator::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-VWR-104 (docs/uat/02-viewer.md) / DR 2026-07-31-transient-zoom-
// readout: opening a zoomable image never itself reveals the transient
// status-bar readout — the indicator stays hidden at rest through the
// open. Triggering an explicit zoom action (Actual Size, here) reveals
// it with the correct percent. Actual Size is the canonical 100% path —
// pin the zoom there so the readout is deterministic (a fit-mode default
// would depend on viewport geometry) — then assert both the "100%" text
// and the general text == qRound(zoomFactor*100) contract the indicator
// upholds.
void TestUatZoomIndicator::uat_zoom_ind_010_actualSizeRevealsCorrectPercent() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_010.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QVERIFY2(doc->supportsZoom(), "A static image document supports zoom");

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY2(indicator, "MainWindow should host a zoomIndicator label");
    QVERIFY2(!indicator->isVisible(),
             "Opening a document must never itself reveal the transient zoom readout "
             "(DR 2026-07-31-transient-zoom-readout)");

    // Pin to Actual Size (Ctrl+0) so the readout is 100% regardless of
    // the viewport-dependent open default. This is an explicit zoom
    // action, so it reveals the readout.
    QAction *actual = actionByText(mw, QStringLiteral("&Actual Size"));
    QVERIFY2(actual, "Actual Size action not found");
    actual->trigger();
    QApplication::processEvents();

    QVERIFY2(indicator->isVisible(),
             "an explicit zoom action must reveal the readout");
    QCOMPARE(doc->zoomFactor(), 1.0);
    QCOMPARE(indicator->text(), expectedPercent(doc));
    QCOMPARE(indicator->text(), QStringLiteral("100%"));
}

// With no document open the indicator has nothing to read out and must
// be hidden. Drives the supportsZoom()==false / no-document branch of
// updateZoomIndicator via the empty-state path: open a document,
// trigger a zoom action to reveal the readout, then close the document
// so the single window persists (Win/Linux) as an empty state — the
// revealed indicator must re-hide.
void TestUatZoomIndicator::uat_zoom_ind_020_noDocumentHidesIndicator() {
#ifdef Q_OS_MACOS
    QSKIP("macOS closes the last window instead of persisting an empty-state window.");
#else
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_020.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->show();
    QApplication::processEvents();
    QCOMPARE(mw->documentCount(), 1);
    QCOMPARE(app->windowCount(), 1);

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY2(indicator, "MainWindow should host a zoomIndicator label");

    QAction *actual = actionByText(mw, QStringLiteral("&Actual Size"));
    QVERIFY(actual);
    actual->trigger();
    QApplication::processEvents();
    QVERIFY2(indicator->isVisible(), "an explicit zoom action must reveal the readout");

    // Close the only document; the single window persists as empty state.
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();

    QCOMPARE(mw->documentCount(), 0);
    QVERIFY2(!zoomIndicator(mw)->isVisible(),
             "With no document open the zoom indicator must be hidden");
#endif
}

// Zooming in updates the readout: from Actual Size (100%) one Zoom In
// step lands on the new percent (25% step -> 125%), and the label
// tracks qRound(zoomFactor*100).
void TestUatZoomIndicator::uat_zoom_ind_030_zoomInUpdatesPercent() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_030.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY(indicator);

    // Start from a known 100%.
    QAction *actual = actionByText(mw, QStringLiteral("&Actual Size"));
    QVERIFY(actual);
    actual->trigger();
    QApplication::processEvents();
    QCOMPARE(indicator->text(), QStringLiteral("100%"));

    QAction *zoomIn = actionByText(mw, QStringLiteral("Zoom &In"));
    QVERIFY2(zoomIn, "Zoom In action not found");
    zoomIn->trigger();
    QApplication::processEvents();

    // The indicator moved off 100% and tracks the document's new factor.
    QVERIFY2(indicator->text() != QStringLiteral("100%"),
             "Zooming in must update the indicator off its previous value");
    QCOMPARE(indicator->text(), expectedPercent(doc));
    QCOMPARE(indicator->text(), QStringLiteral("125%"));
}

// UAT-VWR-103 / DR 2026-07-31-transient-zoom-readout: an explicit zoom
// action reveals the readout immediately, and it fades back out on its
// own after the hold+fade window, with no further zoom action. Uses the
// test-only setZoomIndicatorTimingForTesting() seam for fast,
// deterministic timing instead of sleeping through the real (~1.5s)
// production hold.
void TestUatZoomIndicator::uat_zoom_ind_040_explicitZoomActionRevealsAndFades() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_040.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->setZoomIndicatorTimingForTesting(60, 40);
    mw->show();
    QApplication::processEvents();

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY(indicator);
    QVERIFY2(!indicator->isVisible(),
             "the readout must be hidden at rest, before any explicit zoom action");

    QAction *zoomIn = actionByText(mw, QStringLiteral("Zoom &In"));
    QVERIFY(zoomIn);
    zoomIn->trigger();
    QApplication::processEvents();

    QVERIFY2(indicator->isVisible(),
             "an explicit zoom action must reveal the readout immediately");
    QCOMPARE(indicator->text(), QStringLiteral("125%"));

    QVERIFY2(pumpUntil([&] { return !indicator->isVisible(); }, 2000),
             "the readout must fade out and hide after the hold+fade window");
}

// UAT-VWR-104: opening a document, and switching between two already-
// open documents, never reveals the readout — only an explicit zoom
// action does (see uat_zoom_ind_040 above).
void TestUatZoomIndicator::uat_zoom_ind_050_docOpenAndSwitchStaySilent() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath1 = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_050a.png")));
    const QString imgPath2 = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_050b.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath1});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();
    // Give the async initial-fit landing (staged image open,
    // onDocumentCapabilitiesChanged) a moment to settle -- that call
    // site must also stay silent.
    QTest::qWait(100);
    QApplication::processEvents();

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY(indicator);
    QVERIFY2(!indicator->isVisible(),
             "opening a document must never itself reveal the readout");

    // A second document opened into the same window (tab) and a tab
    // switch back to the first must also stay silent.
    app->openFiles({imgPath2});
    QApplication::processEvents();
    QTest::qWait(100);
    QApplication::processEvents();
    QVERIFY2(!indicator->isVisible(), "opening a second document must stay silent");

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    if (dv->count() > 1) {
        dv->setCurrentIndex(0);
        QApplication::processEvents();
        QVERIFY2(!indicator->isVisible(), "switching tabs must stay silent");
    }
}

// UAT-VWR-105: with Reduce Motion on, the readout hides instantly at the
// end of its hold -- no opacity fade plays. Uses the cross-platform
// trailer::platform::setReducedMotionOverrideForTest() seam so this is
// deterministic on every platform (including macOS, where the real
// query would otherwise read the developer/CI machine's own
// accessibility setting).
void TestUatZoomIndicator::uat_zoom_ind_060_reduceMotionSkipsFade() {
    const auto resetOverride =
        qScopeGuard([] { trailer::platform::setReducedMotionOverrideForTest(std::nullopt); });
    trailer::platform::setReducedMotionOverrideForTest(true);

    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_060.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    // A short hold, but a LONG fade duration: with Reduce Motion honoured
    // the fade never starts at all, so the label is hidden well before a
    // 5000ms fade could ever finish. If the production code regressed
    // and animated anyway, this pump would time out and fail loudly
    // instead of passing by accident.
    mw->setZoomIndicatorTimingForTesting(60, 5000);
    mw->show();
    QApplication::processEvents();

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY(indicator);

    QAction *zoomIn = actionByText(mw, QStringLiteral("Zoom &In"));
    QVERIFY(zoomIn);
    zoomIn->trigger();
    QApplication::processEvents();
    QVERIFY(indicator->isVisible());

    QVERIFY2(pumpUntil([&] { return !indicator->isVisible(); }, 500),
             "Reduce Motion must hide the readout promptly once the hold elapses, "
             "without an opacity fade");
}

// UAT-VWR-106 / G10 (spatial constancy, AGENTS.md): toggling the
// transient readout's visibility must never move an EXISTING sibling
// status-bar widget. m_zoomIndicator is deliberately the rightmost
// permanent status-bar widget (see the addPermanentWidget() call site
// comment in MainWindow's constructor) so nothing sits to its right to
// be displaced when it reveals/hides; this pins that a widget POSITIONED
// BEFORE it (here, the Two-Pages read-only badge, forced visible
// directly since its normal trigger is Two-Pages mode) keeps an
// unchanged pos() across a reveal-then-hide cycle.
void TestUatZoomIndicator::uat_zoom_ind_070_togglingReadoutDoesNotShiftSiblingWidgets() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("zoom_ind_070.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->setZoomIndicatorTimingForTesting(60, 40);
    mw->show();
    QApplication::processEvents();

    QLabel *indicator = zoomIndicator(mw);
    QVERIFY(indicator);
    QVERIFY2(!indicator->isVisible(), "resting state: readout hidden");

    auto *sibling = mw->findChild<QLabel *>(QStringLiteral("twoPageReadOnlyBadge"));
    QVERIFY2(sibling, "MainWindow should host the read-only badge label");
    sibling->setVisible(true); // force-visible for this test; sits to the readout's left
    QApplication::processEvents();
    const QPoint posBeforeReveal = sibling->pos();

    QAction *zoomIn = actionByText(mw, QStringLiteral("Zoom &In"));
    QVERIFY(zoomIn);
    zoomIn->trigger();
    QApplication::processEvents();
    QVERIFY2(indicator->isVisible(), "sanity: the readout actually revealed");

    QCOMPARE(sibling->pos(), posBeforeReveal);

    // ...and again once it fades back out.
    QVERIFY2(pumpUntil([&] { return !indicator->isVisible(); }, 2000),
             "sanity: the readout actually faded back out");
    QCOMPARE(sibling->pos(), posBeforeReveal);
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir.
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
    TestUatZoomIndicator tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_zoom_indicator.moc"
