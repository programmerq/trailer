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
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QImage>
#include <QLabel>
#include <QPointer>
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

    void uat_zoom_ind_010_imageOpenShowsPercent();
    void uat_zoom_ind_020_noDocumentHidesIndicator();
    void uat_zoom_ind_030_zoomInUpdatesPercent();

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

// Opening a zoomable image surfaces the indicator: the label is visible
// and reads the current zoom as an integer percent. Actual Size is the
// canonical 100% path — pin the zoom there first so the readout is
// deterministic (a fit-mode default would depend on viewport geometry),
// then assert both the "100%" text and the general
// text == qRound(zoomFactor*100) contract the indicator upholds.
void TestUatZoomIndicator::uat_zoom_ind_010_imageOpenShowsPercent() {
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
    QVERIFY2(indicator, "MainWindow should host a permanent zoomIndicator label");
    QVERIFY2(indicator->isVisible(),
             "The zoom indicator must be visible while a zoomable document is open");

    // Pin to Actual Size (Ctrl+0) so the readout is 100% regardless of
    // the viewport-dependent open default, then assert the exact text.
    QAction *actual = actionByText(mw, QStringLiteral("&Actual Size"));
    QVERIFY2(actual, "Actual Size action not found");
    actual->trigger();
    QApplication::processEvents();

    QCOMPARE(doc->zoomFactor(), 1.0);
    QCOMPARE(indicator->text(), expectedPercent(doc));
    QCOMPARE(indicator->text(), QStringLiteral("100%"));
}

// With no document open the indicator has nothing to read out and must
// be hidden. Drives the supportsZoom()==false / no-document branch of
// updateZoomIndicator via the empty-state path: open the only document,
// then close it so the single window persists (Win/Linux) as an empty
// state — the indicator, visible while the doc was open, must re-hide.
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
    QVERIFY2(indicator, "MainWindow should host a permanent zoomIndicator label");
    QVERIFY2(indicator->isVisible(),
             "Opening a zoomable document must show the indicator");

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
