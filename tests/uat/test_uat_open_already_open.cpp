// UAT harness — "opening an already-open file surfaces the existing
// window" (UAT-FND-053..058).
//
// Owner HITL report, 2026-08-06 (macOS): typing a PDF's name into
// Spotlight while Trailer already had that exact file open produced a
// SECOND window showing the same file. Two IDocument instances over one
// path means two undo logs and two save paths onto the same bytes — the
// document stops being the subject and starts being a copy.
//
// This file deliberately calls ONLY stable, pre-existing public API
// (Application::openFiles / newFromClipboard / windowCount / settings,
// MainWindow::documentCount / documentAt / currentDocumentIndex) — no
// symbol this PR introduces (Application::windowForOpenPath,
// MainWindow::showDocumentAt, trailer::canonicalPathKey) — so this SAME
// file builds and runs unmodified against the tree BEFORE and AFTER the
// fix. That is what makes the G2 evidence a genuine before/after pair
// rather than two unrelated captures, and it is the same discipline
// test_uat_deference_evidence.cpp follows.
//
// The evidence slot emits its PNG to $TRAILER_OPENDEDUP_EVIDENCE_DIR when
// set and QSKIPs when unset, matching the evidence-dir convention used
// across this suite. The behavioural slots always assert.
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a throwaway
// sandbox and every case starts from a no-window baseline.

#include "app/Application.h"
#include "document/IDocument.h"
#include "settings/Settings.h"
#include "ui/MainWindow.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

// Fixed window size for the evidence composite so the committed
// before/after PNGs are directly comparable frame-for-frame.
constexpr int kEvidenceWindowW = 720;
constexpr int kEvidenceWindowH = 520;
constexpr int kEvidenceGap = 18;
constexpr int kEvidenceCaptionH = 22;

QString writeTinyPdf(const QString &path, const QString &caption) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont f;
    f.setPointSize(22);
    p.setFont(f);
    p.drawText(300, 500, caption);
    p.end();
    return path;
}

QString writeTinyPng(const QString &path) {
    QImage img(80, 60, QImage::Format_RGB32);
    img.fill(Qt::white);
    img.save(path, "PNG");
    return path;
}

void setClipboardImage() {
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(img);
}

// The on-disk path of the document at `index` of `mw`, or a null string.
QString docPathAt(MainWindow *mw, int index) {
    IDocument *doc = nullptr;
    if (!mw || !mw->documentAt(index, &doc) || !doc)
        return QString();
    return doc->filePath();
}

// The on-disk path of the document the user is actually looking at in
// `mw` — the current tab. Null when the window holds nothing.
QString currentDocPath(MainWindow *mw) {
    if (!mw)
        return QString();
    return docPathAt(mw, mw->currentDocumentIndex());
}

// Every live MainWindow, in creation order. Deliberately walks
// topLevelWidgets rather than Application::windows() so the helper needs
// no API beyond QApplication — see the file header on before/after
// buildability.
QList<MainWindow *> liveWindows() {
    QList<MainWindow *> out;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            out.append(mw);
    }
    return out;
}

QString evidenceDir() {
    const QByteArray dir = qgetenv("TRAILER_OPENDEDUP_EVIDENCE_DIR");
    if (dir.isEmpty())
        return QString();
    const QString path = QString::fromLocal8Bit(dir);
    QDir().mkpath(path);
    return path;
}

// Compose every live window side by side into one PNG, each under a
// caption strip carrying that window's LIVE windowTitle(). The BEFORE
// tree leaves two windows here and the AFTER tree one, so a single
// composite shows the whole defect and the whole fix in the same frame —
// the same-state requirement G2's before/after rule imposes.
//
// The caption strip exists because QWidget::grab() captures the client
// area only: the OS title bar (where the document's name lives, and
// where "two windows, one file" is legible at a glance) is not part of
// the widget. The text is read from the window itself, never invented.
void grabAllWindows(const QString &dir, const QString &name) {
    const QList<MainWindow *> wins = liveWindows();
    if (dir.isEmpty() || wins.isEmpty())
        return;

    QList<QPixmap> shots;
    int totalW = 0;
    int maxH = 0;
    for (MainWindow *mw : wins) {
        const QPixmap shot = mw->grab();
        shots.append(shot);
        totalW += shot.width();
        maxH = qMax(maxH, shot.height());
    }
    totalW += kEvidenceGap * (static_cast<int>(shots.size()) + 1);
    maxH += kEvidenceGap * 2 + kEvidenceCaptionH;

    QImage canvas(totalW, maxH, QImage::Format_RGB32);
    canvas.fill(QColor(0x60, 0x60, 0x66)); // desktop-ish backdrop
    QPainter p(&canvas);
    QFont caption = p.font();
    caption.setPointSize(11);
    caption.setBold(true);
    p.setFont(caption);

    int x = kEvidenceGap;
    for (int i = 0; i < shots.size(); ++i) {
        const QPixmap &shot = shots.at(i);
        const int shotY = kEvidenceGap + kEvidenceCaptionH;

        // Caption strip: the window's own title, i.e. which file this
        // frame claims to be showing.
        p.setPen(QPen(QColor(0xf2, 0xf2, 0xf4), 1));
        p.drawText(QRect(x, kEvidenceGap, shot.width(), kEvidenceCaptionH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Window %1 — %2")
                       .arg(i + 1)
                       .arg(wins.at(i)->windowTitle()));

        p.drawPixmap(x, shotY, shot);
        p.setPen(QPen(QColor(0x20, 0x20, 0x24), 1));
        p.drawRect(x - 1, shotY - 1, shot.width() + 1, shot.height() + 1);
        x += shot.width() + kEvidenceGap;
    }
    p.end();

    const QString path = QDir(dir).absoluteFilePath(name);
    canvas.save(path, "PNG");
    qInfo().noquote() << "G2-SCREENSHOT" << path << "windows=" << wins.size();
}

} // namespace

class TestUatOpenAlreadyOpen : public QObject {
    Q_OBJECT

  private:
    QTemporaryDir m_scratch;

  private slots:
    void init();

    void uat_fnd_053_reopenSameFileSurfacesExistingWindow();
    void uat_fnd_054_reopenSameFileInNewTabMode();
    void uat_fnd_055_reopenSameFileInSameWindowMode();
    void uat_fnd_056_symlinkAndTargetAreOneDocument();
    void uat_fnd_057_mixedBatchOpensNewAndSurfacesExisting();
    void uat_fnd_058_untitledImportsNeverDedup();
    void uat_fnd_053_090_openAlreadyOpenEvidence();
};

void TestUatOpenAlreadyOpen::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
            mw->close();
        }
    }
    QApplication::processEvents();
    // Every slot below states its own mode; reset to the shipped default
    // (NewWindow) so a slot that changes it cannot leak into the next.
    if (auto *app = qobject_cast<Application *>(qApp))
        app->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
}

// UAT-FND-053 — NewWindow mode. Asking to open a file that is already
// open must surface the window that already holds it: no second window,
// no second copy of the document, and that document is the current tab.
void TestUatOpenAlreadyOpen::uat_fnd_053_reopenSameFileSurfacesExistingWindow() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeTinyPdf(m_scratch.filePath("uat_fnd_053.pdf"),
                                     QStringLiteral("UAT-FND-053"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::NewWindow);

    app->openFiles({pdf});
    QApplication::processEvents();
    QCOMPARE(app->windowCount(), 1);
    MainWindow *first = liveWindows().value(0);
    QVERIFY(first);
    QCOMPARE(first->documentCount(), 1);
    QPointer<MainWindow> guard(first);

    // The user asks for the same file again (Spotlight / Recent / Finder).
    app->openFiles({pdf});
    QApplication::processEvents();

    QCOMPARE(app->windowCount(), 1);
    QVERIFY2(!guard.isNull(), "The window already holding the file must survive");
    QCOMPARE(liveWindows().value(0), first);
    QVERIFY2(first->documentCount() == 1,
             "A second IDocument over the same path is the defect — two undo "
             "logs and two save paths onto one file");
    QCOMPARE(currentDocPath(first), pdf);
}

// UAT-FND-054 — NewTab mode. Dedup precedes mode routing: the setting
// governs where a NEW document lands, not whether one document may exist
// twice, so an already-open file gains no tab.
void TestUatOpenAlreadyOpen::uat_fnd_054_reopenSameFileInNewTabMode() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeTinyPdf(m_scratch.filePath("uat_fnd_054.pdf"),
                                     QStringLiteral("UAT-FND-054"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::NewTab);

    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);

    app->openFiles({pdf});
    QApplication::processEvents();

    QCOMPARE(app->windowCount(), 1);
    QCOMPARE(mw->documentCount(), 1);
    QCOMPARE(currentDocPath(mw), pdf);
}

// UAT-FND-055 — SameWindow mode. Same rule, third routing mode.
void TestUatOpenAlreadyOpen::uat_fnd_055_reopenSameFileInSameWindowMode() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeTinyPdf(m_scratch.filePath("uat_fnd_055.pdf"),
                                     QStringLiteral("UAT-FND-055"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::SameWindow);

    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);

    app->openFiles({pdf});
    QApplication::processEvents();

    QCOMPARE(app->windowCount(), 1);
    QCOMPARE(mw->documentCount(), 1);
    QCOMPARE(currentDocPath(mw), pdf);
}

// UAT-FND-056 — A symlink and its target are ONE document. Opening the
// link after the target (or vice versa) surfaces the open document
// rather than opening the same bytes a second time under a second name.
void TestUatOpenAlreadyOpen::uat_fnd_056_symlinkAndTargetAreOneDocument() {
#ifdef Q_OS_WIN
    QSKIP("Symlink creation on Windows needs developer mode / elevation.");
#else
    QVERIFY(m_scratch.isValid());
    const QString target = writeTinyPdf(m_scratch.filePath("uat_fnd_056_target.pdf"),
                                        QStringLiteral("UAT-FND-056"));
    const QString link = m_scratch.filePath("uat_fnd_056_link.pdf");
    QVERIFY2(QFile::link(target, link), "could not create the symlink fixture");

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::NewWindow);

    app->openFiles({target});
    QApplication::processEvents();
    QCOMPARE(app->windowCount(), 1);
    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);

    // Same bytes, different name — still one document.
    app->openFiles({link});
    QApplication::processEvents();

    QCOMPARE(app->windowCount(), 1);
    QCOMPARE(mw->documentCount(), 1);
#endif
}

// UAT-FND-057 — Mixed batch. Two new files plus one already-open file:
// the two new ones open normally, the third is surfaced, and the LAST
// path in the batch decides what ends up in front (here: the surfaced
// one, so its window is frontmost with that document current).
void TestUatOpenAlreadyOpen::uat_fnd_057_mixedBatchOpensNewAndSurfacesExisting() {
    QVERIFY(m_scratch.isValid());
    const QString openAlready = writeTinyPdf(m_scratch.filePath("uat_fnd_057_a.pdf"),
                                             QStringLiteral("UAT-FND-057 a"));
    const QString newB = writeTinyPdf(m_scratch.filePath("uat_fnd_057_b.pdf"),
                                      QStringLiteral("UAT-FND-057 b"));
    const QString newC = writeTinyPdf(m_scratch.filePath("uat_fnd_057_c.pdf"),
                                      QStringLiteral("UAT-FND-057 c"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::NewWindow);

    app->openFiles({openAlready});
    QApplication::processEvents();
    QCOMPARE(app->windowCount(), 1);
    MainWindow *host = liveWindows().value(0);
    QVERIFY(host);

    // b and c are new; a is already open. Expect +2 windows, not +3.
    app->openFiles({newB, newC, openAlready});
    QApplication::processEvents();

    QCOMPARE(app->windowCount(), 3);
    QCOMPARE(host->documentCount(), 1);
    QCOMPARE(currentDocPath(host), openAlready);

    // Exactly one window per distinct file — no path opened twice.
    QStringList seen;
    for (MainWindow *mw : liveWindows()) {
        for (int i = 0; i < mw->documentCount(); ++i)
            seen.append(docPathAt(mw, i));
    }
    seen.sort();
    QStringList expected{openAlready, newB, newC};
    expected.sort();
    QCOMPARE(seen, expected);
}

// UAT-FND-058 — Transient imports never dedup. A clipboard/screenshot
// import is backed by a temp file the user never chose; two pastes are
// two documents even though neither has a chosen location. Nothing here
// may collapse them into one.
void TestUatOpenAlreadyOpen::uat_fnd_058_untitledImportsNeverDedup() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::NewTab);

    setClipboardImage();
    app->newFromClipboard();
    QApplication::processEvents();
    setClipboardImage();
    app->newFromClipboard();
    QApplication::processEvents();

    int totalDocs = 0;
    for (MainWindow *mw : liveWindows())
        totalDocs += mw->documentCount();
    QCOMPARE(totalDocs, 2);
}

// G2 evidence (before/after pair). Runs the exact reported flow — open a
// file, then ask to open that same file again — and composites every
// resulting window into one PNG. Built against the pre-fix tree it yields
// the two-window "looks like two open files" frame; against the fixed
// tree, one window with the document current.
void TestUatOpenAlreadyOpen::uat_fnd_053_090_openAlreadyOpenEvidence() {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        QSKIP("Set TRAILER_OPENDEDUP_EVIDENCE_DIR to emit the G2 evidence PNG.");

    QVERIFY(m_scratch.isValid());
    // A recognisable, owner-report-shaped name so the shot reads as the
    // reported flow rather than a synthetic fixture.
    const QString pdf =
        writeTinyPdf(m_scratch.filePath("Electrical service manual.pdf"),
                     QStringLiteral("Electrical service manual"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setOpenFilesIn(OpenFilesIn::NewWindow);

    app->openFiles({pdf});
    QApplication::processEvents();
    for (MainWindow *mw : liveWindows())
        mw->resize(kEvidenceWindowW, kEvidenceWindowH);
    QApplication::processEvents();

    // The reported action: pick the SAME file again from Spotlight.
    app->openFiles({pdf});
    QApplication::processEvents();
    for (MainWindow *mw : liveWindows())
        mw->resize(kEvidenceWindowW, kEvidenceWindowH);
    QApplication::processEvents();

    grabAllWindows(dir, QStringLiteral("open-already-open.png"));
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir. See
// tests/test_image_scale.cpp's main() for why QSettings::IniFormat is
// forced (NativeFormat on macOS ignores the HOME sandbox).
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
    TestUatOpenAlreadyOpen tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_open_already_open.moc"
