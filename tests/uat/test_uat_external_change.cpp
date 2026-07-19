// UAT harness — External file-change handling (ADR 2026-07-19)
//
// Drives the Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen. Covers the behaviour matrix from
// docs/decision-records/2026-07-19-external-file-change-handling.md:
//   * clean doc + external change  -> silent auto-reload (no banner)
//   * dirty doc + external change  -> non-modal conflict banner
//   * deleted on disk              -> deleted banner, buffer kept
//   * Compare is a disabled G3 placeholder
//
// It also emits the G2 before/after evidence PNGs (normal document view vs.
// the same view with the conflict banner) when TRAILER_EXTCHANGE_EVIDENCE_DIR
// is set; otherwise it just asserts the wired behaviour like any other slot.
//
// Labelled `uat` in CTest (runs under `ctest -L uat`).

#include "app/Application.h"
#include "document/ExternalChangeMonitor.h"
#include "document/IDocument.h"
#include "ui/FileChangeBanner.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QFile>
#include <QImage>
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

QString writeImage(const QString &path, QSize size, const QColor &color) {
    QImage img(size, QImage::Format_ARGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

// Curated G2 evidence dir (opt-in): committed PNGs are produced by pointing
// this at docs/uat/images/. Absent -> the slot still asserts behaviour.
QString evidenceDir() { return qEnvironmentVariable("TRAILER_EXTCHANGE_EVIDENCE_DIR"); }

void grabTo(QWidget *w, const QString &name) {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString path = QDir(dir).absoluteFilePath(name);
    w->grab().save(path, "PNG");
    qInfo().noquote() << "G2-SCREENSHOT" << path;
}

} // namespace

class TestUatExternalChange : public QObject {
    Q_OBJECT

  private:
    QTemporaryDir m_dir;

    IDocument *currentDoc(MainWindow *mw) {
        IDocument *doc = nullptr;
        mw->documentAt(0, &doc);
        return doc;
    }

  private slots:
    // The default open mode is NewWindow, so every slot's openFiles() spawns a
    // fresh frame. Without closing the previous slot's window first,
    // currentMainWindow() (which returns the oldest MainWindow in
    // topLevelWidgets) resolves to a stale window whose current document and
    // external-change monitor belong to an earlier slot — the same isolation
    // that test_uat_recognize_text's init() enforces. Our slots leave the
    // document dirty (a rotate makes the conflict realistic), so force Discard
    // on close to avoid the unsaved-changes modal blocking headlessly.
    void init() {
        for (auto *w : QApplication::topLevelWidgets()) {
            if (auto *mw = qobject_cast<MainWindow *>(w)) {
                mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
                mw->close();
            }
        }
        QApplication::processEvents();
    }

    // UAT-EXT-001: dirty doc + external change -> non-modal conflict banner
    // with Reload + Keep mine, and a DISABLED Compare placeholder (G3). Emits
    // the G2 before/after evidence pair.
    void uat_ext_001_dirtyConflictShowsBanner() {
        auto *app = qobject_cast<Application *>(qApp);
        QVERIFY(app);
        const QString path =
            writeImage(m_dir.filePath(QStringLiteral("ext001.png")), QSize(400, 300), Qt::white);
        app->openFiles({path});
        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        mw->resize(1000, 700);
        QApplication::processEvents();

        auto *banner = mw->findChild<FileChangeBanner *>();
        auto *mon = mw->findChild<ExternalChangeMonitor *>();
        QVERIFY(banner);
        QVERIFY(mon);
        mon->setDebounceMsForTest(10);
        QCOMPARE(banner->mode(), FileChangeBanner::Mode::Hidden);

        // BEFORE: normal document view (no banner).
        grabTo(mw, QStringLiteral("external-change-conflict-banner-before.png"));

        // Make an edit (dirty), then simulate another program overwriting the
        // file with different content/size.
        IDocument *doc = currentDoc(mw);
        QVERIFY(doc);
        doc->rotatePage(0, 90);
        QVERIFY(doc->isDirty());
        writeImage(path, QSize(640, 480), Qt::darkCyan);

        // Drive the monitor as a real filesystem event would.
        mon->pokeForTest();
        QTRY_COMPARE_WITH_TIMEOUT(banner->mode(), FileChangeBanner::Mode::Conflict, 2000);

        // G3: Compare is present but disabled with an explanatory tooltip.
        QVERIFY(!banner->compareEnabled());
        QVERIFY(!banner->compareTooltip().isEmpty());
        QVERIFY(banner->reloadEnabled());
        QVERIFY(banner->keepMineEnabled());

        QApplication::processEvents();
        // AFTER: same window/state with the conflict banner shown.
        grabTo(mw, QStringLiteral("external-change-conflict-banner-after.png"));
    }

    // UAT-EXT-002: clean doc + external change -> silent reload, no banner,
    // and the reloaded content reflects the new bytes.
    void uat_ext_002_cleanChangeReloadsSilently() {
        auto *app = qobject_cast<Application *>(qApp);
        const QString path =
            writeImage(m_dir.filePath(QStringLiteral("ext002.png")), QSize(120, 90), Qt::white);
        app->openFiles({path});
        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        auto *banner = mw->findChild<FileChangeBanner *>();
        auto *mon = mw->findChild<ExternalChangeMonitor *>();
        mon->setDebounceMsForTest(10);

        IDocument *doc = currentDoc(mw);
        QVERIFY(doc);
        QVERIFY(!doc->isDirty());
        writeImage(path, QSize(200, 150), Qt::magenta);
        mon->pokeForTest();

        // Give the debounce + reload a chance to run, then assert the banner
        // stayed hidden and the document picked up the new size.
        QTRY_COMPARE_WITH_TIMEOUT(doc->imagePixelSize(), QSize(200, 150), 2000);
        QCOMPARE(banner->mode(), FileChangeBanner::Mode::Hidden);
    }

    // UAT-EXT-003: file deleted on disk -> deleted banner, buffer kept.
    void uat_ext_003_deletedShowsBannerAndKeepsBuffer() {
        auto *app = qobject_cast<Application *>(qApp);
        const QString path =
            writeImage(m_dir.filePath(QStringLiteral("ext003.png")), QSize(120, 90), Qt::white);
        app->openFiles({path});
        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        mw->resize(1000, 700);
        QApplication::processEvents();
        auto *banner = mw->findChild<FileChangeBanner *>();
        auto *mon = mw->findChild<ExternalChangeMonitor *>();
        mon->setDebounceMsForTest(10);

        IDocument *doc = currentDoc(mw);
        QVERIFY(doc);
        QVERIFY(QFile::remove(path));
        mon->pokeForTest();
        QTRY_COMPARE_WITH_TIMEOUT(banner->mode(), FileChangeBanner::Mode::Deleted, 2000);
        QVERIFY(banner->saveEnabled());
        // Buffer is kept — the document still has its content.
        QCOMPARE(doc->pageCount(), 1);

        QApplication::processEvents();
        // G2 evidence: the deleted-on-disk marker banner (Save recreates).
        grabTo(mw, QStringLiteral("external-change-deleted-marker.png"));
    }

    // UAT-EXT-004: the banner's Reload action reloads from disk and dismisses.
    void uat_ext_004_reloadActionTakesDiskCopy() {
        auto *app = qobject_cast<Application *>(qApp);
        const QString path =
            writeImage(m_dir.filePath(QStringLiteral("ext004.png")), QSize(100, 100), Qt::white);
        app->openFiles({path});
        MainWindow *mw = currentMainWindow();
        auto *banner = mw->findChild<FileChangeBanner *>();
        auto *mon = mw->findChild<ExternalChangeMonitor *>();
        mon->setDebounceMsForTest(10);

        IDocument *doc = currentDoc(mw);
        doc->rotatePage(0, 90);
        writeImage(path, QSize(300, 200), Qt::darkGreen);
        mon->pokeForTest();
        QTRY_COMPARE_WITH_TIMEOUT(banner->mode(), FileChangeBanner::Mode::Conflict, 2000);

        banner->clickReloadForTest();
        QApplication::processEvents();
        QCOMPARE(banner->mode(), FileChangeBanner::Mode::Hidden);
        QCOMPARE(doc->imagePixelSize(), QSize(300, 200));
        QVERIFY(!doc->isDirty());
    }
};

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
    TestUatExternalChange tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_external_change.moc"
