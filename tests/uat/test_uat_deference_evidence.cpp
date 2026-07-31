// UAT harness — G2 before/after evidence for the "deference" PR (G10):
// PDF surround colour (item 1), the silenced "Recovery snapshot saved"
// toast (item 2), and the textless Sidebar/Inspector title bars (item 3).
//
// This file deliberately calls ONLY stable, pre-existing public API
// (Application::openFiles/applyTheme, MainWindow, QPdfView, QScrollArea,
// QDockWidget) — no symbol introduced by this PR (documentSurroundColor(),
// IDocument::refreshViewPalette(), buildTextlessDockTitleBar()) — so this
// SAME file can be built and run unmodified against the tree BEFORE and
// AFTER the fix to produce a genuine before/after pair, not two unrelated
// captures. Emits PNGs to $TRAILER_DEFERENCE_EVIDENCE_DIR when set;
// QSKIPs (no assertions, no failure) when unset, matching the
// TRAILER_PREF_EVIDENCE_DIR / TRAILER_STAGED_OPEN_EVIDENCE_DIR convention
// used elsewhere in this suite.
//
// Mirrors the custom-main + HOME-sandbox scaffolding of
// test_uat_preferences.cpp.

#include "app/Application.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPen>
#include <QScrollArea>
#include <QPdfWriter>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
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

QString evidenceDir() {
    const QByteArray dir = qgetenv("TRAILER_DEFERENCE_EVIDENCE_DIR");
    if (dir.isEmpty())
        return QString();
    const QString path = QString::fromLocal8Bit(dir);
    QDir().mkpath(path);
    return path;
}

// A representative dark palette — same rationale as
// test_uat_preferences.cpp's darkPalette(): the offscreen QPA plugin has no
// platform theme, so QStyleHints::setColorScheme cannot derive a genuinely
// dark palette on its own; this is the sanctioned offscreen substitute so
// the "dark" evidence shot is actually dark, per AGENTS.md's G2 capture-
// method note.
QPalette darkPalette() {
    QPalette p;
    const QColor window(0x2b, 0x2b, 0x2e);
    const QColor base(0x1e, 0x1e, 0x21);
    const QColor text(0xe6, 0xe6, 0xe6);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, QColor(0x3d, 0x6e, 0xb4));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    return p;
}

QString writeEvidencePdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.setPen(QPen(Qt::black, 2));
    p.drawText(200, 200, QStringLiteral("Deference PR — G2 evidence fixture"));
    p.drawRect(100, 100, 1200, 1600);
    return path;
}

} // namespace

class TestUatDeferenceEvidence : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_evidence_pdfSurroundLightAndDark();
    void uat_evidence_dockPanelCaptions();
    void uat_evidence_recoverySnapshotStatusBar();

  private:
    QTemporaryDir m_scratch;
};

void TestUatDeferenceEvidence::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// Item 1 — G2 evidence: a PDF zoomed out enough to show visible surround
// margin, captured in light then dark, with the SAME document and window
// in both. Run this same file against the tree before and after the fix
// (see the file header) to get the before/after pair G2 requires.
void TestUatDeferenceEvidence::uat_evidence_pdfSurroundLightAndDark() {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        QSKIP("TRAILER_DEFERENCE_EVIDENCE_DIR not set; skipping evidence capture");

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QPalette lightPalette = qApp->palette();

    const QString pdfPath = writeEvidencePdf(m_scratch.filePath(QStringLiteral("evidence.pdf")));
    app->openFiles({pdfPath});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1000, 800);
    mw->show();
    QApplication::processEvents();

    auto *pdfView = mw->findChild<QPdfView *>();
    QVERIFY(pdfView);
    // Zoom out well below fit-width so the page is clearly smaller than the
    // viewport on every edge — the surround must be visible for this shot
    // to mean anything.
    pdfView->setZoomMode(QPdfView::ZoomMode::Custom);
    pdfView->setZoomFactor(0.35);
    QApplication::processEvents();

    qApp->setPalette(lightPalette);
    app->applyTheme(Theme::Light);
    QApplication::processEvents();
    QVERIFY(mw->grab().save(dir + "/pdf-surround-light.png"));

    qApp->setPalette(darkPalette());
    app->applyTheme(Theme::Dark);
    QApplication::processEvents();
    QVERIFY(mw->grab().save(dir + "/pdf-surround-dark.png"));

    // Restore ambient state for any later slot.
    qApp->setPalette(lightPalette);
    app->applyTheme(Theme::System);
    QApplication::processEvents();
}

// Item 3 — G2 evidence: MainWindow with both Sidebar and Inspector open,
// same document/window in both captures.
void TestUatDeferenceEvidence::uat_evidence_dockPanelCaptions() {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        QSKIP("TRAILER_DEFERENCE_EVIDENCE_DIR not set; skipping evidence capture");

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString pdfPath =
        writeEvidencePdf(m_scratch.filePath(QStringLiteral("evidence-docks.pdf")));
    app->openFiles({pdfPath});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1200, 800);
    mw->show();

    auto *sidebar = mw->findChild<QDockWidget *>(QStringLiteral("trailer.sidebar"));
    auto *inspector = mw->findChild<QDockWidget *>(QStringLiteral("trailer.inspector"));
    QVERIFY(sidebar);
    QVERIFY(inspector);
    sidebar->show();
    inspector->show();
    QApplication::processEvents();

    QVERIFY(mw->grab().save(dir + "/dock-panels.png"));
}

// Item 2 — G2 evidence: the status bar immediately after an auto-save tick.
// Same window/document in both captures; the only thing that should ever
// differ is whether the toast text appears.
void TestUatDeferenceEvidence::uat_evidence_recoverySnapshotStatusBar() {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        QSKIP("TRAILER_DEFERENCE_EVIDENCE_DIR not set; skipping evidence capture");

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);

    const QString pdfPath =
        writeEvidencePdf(m_scratch.filePath(QStringLiteral("evidence-autosave.pdf")));
    app->openFiles({pdfPath});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1000, 700);
    mw->show();

    auto *pdfView = mw->findChild<QPdfView *>();
    QVERIFY(pdfView);

    // Dirty the doc via the real Rotate Left action so the title-bar dirty
    // marker is genuinely present in both shots (proving nothing was lost).
    QAction *rotateLeft = nullptr;
    for (QAction *a : mw->findChildren<QAction *>()) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t.compare(QStringLiteral("Rotate Left"), Qt::CaseInsensitive) == 0) {
            rotateLeft = a;
            break;
        }
    }
    QVERIFY2(rotateLeft, "Rotate Left action not found");
    QVERIFY2(rotateLeft->isEnabled(), "Rotate Left should be enabled for a valid PDF");
    rotateLeft->trigger();
    QApplication::processEvents();
    const QString kDot = QString::fromUtf8("\xE2\x80\xA2");
    QVERIFY2(mw->windowTitle().contains(kDot),
             qPrintable(QStringLiteral("dirty marker not showing before auto-save; title was '%1'")
                            .arg(mw->windowTitle())));

    mw->autoSaveDirtyDocs();
    QApplication::processEvents();
    QVERIFY2(mw->windowTitle().contains(kDot), "dirty marker must survive the auto-save tick");
    QVERIFY(mw->grab().save(dir + "/statusbar-after-autosave.png"));
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
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Application app(argc, argv);
    TestUatDeferenceEvidence tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_deference_evidence.moc"
