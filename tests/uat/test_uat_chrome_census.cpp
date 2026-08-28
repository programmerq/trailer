// UAT harness — Chrome census (UAT-XCT-090, docs/uat/06-cross-cutting.md).
//
// The deference half of G10, made machine-checkable: every canonical
// at-rest state's permanent chrome surface is walked from the realized
// widget tree (uat_chrome_walk.h — a TREE walk, not a registry, so
// floating move()-positioned overlays are seen) and diffed verbatim
// against the committed golden docs/uat/chrome-census.json. A build
// whose at-rest surface contains an element the golden does not list
// FAILS — new permanent chrome cannot land silently; it lands as a
// one-line golden diff the owner explicitly acknowledges.
//
// Canonical states (in this order — the empty-window census must run
// before any document opens, so the sandboxed Recent list is still
// empty and the EmptyStateWidget's dynamic recent-file buttons stay
// out of the walk):
//   empty-window   ensureWindow() with no document (macOS: QSKIP —
//                  DESIGN §2.4.2 shows no window there)
//   pdf-open       a one-page QPdfWriter PDF
//   image-open     a static PNG
//   form-pdf-open  a one-page AcroForm PDF (text + checkbox + dropdown,
//                  the test_uat_forms fixture) — the state where the
//                  form toolbar auto-enables
//
// GOLDEN OWNER-ACK RULE (also in the golden's _readme): a diff to
// docs/uat/chrome-census.json merges only with explicit owner
// acknowledgment and a cited accepted decision record. The 2026-08-28
// baseline is a faithful freeze of the surface as built; pruning rows
// is a later owner decision.
//
// Regenerate after an owner-approved chrome change:
//   TRAILER_CENSUS_WRITE=1 QT_QPA_PLATFORM=offscreen ./test_uat_chrome_census
// (writes the golden in-place under the source tree, then still runs
// green; commit the diff in the PR that carries the owner's ack.)

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include "uat_chrome_walk.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QLabel>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>

using namespace trailer;

#ifndef TRAILER_CENSUS_GOLDEN_DIR
#error "TRAILER_CENSUS_GOLDEN_DIR must point at <source>/docs/uat (set in tests/uat/CMakeLists.txt)"
#endif

namespace {

const QString kGoldenPath =
    QStringLiteral(TRAILER_CENSUS_GOLDEN_DIR) + QStringLiteral("/chrome-census.json");

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("Census fixture"));
    p.end();
    return path;
}

QString writeStaticImage(const QString &path) {
    QImage img(200, 150, QImage::Format_ARGB32);
    img.fill(qRgb(200, 210, 220));
    img.save(path, "PNG");
    return path;
}

// One-page AcroForm PDF — the test_uat_forms.cpp fixture (text field +
// checkbox + dropdown) so the census sees the same form-PDF state the
// forms UAT drives.
QString writeFormPdf(const QString &path) {
    QPDF pdf;
    pdf.emptyPDF();

    auto makeRect = [](double x0, double y0, double x1, double y1) {
        QPDFObjectHandle r = QPDFObjectHandle::newArray();
        r.appendItem(QPDFObjectHandle::newReal(x0));
        r.appendItem(QPDFObjectHandle::newReal(y0));
        r.appendItem(QPDFObjectHandle::newReal(x1));
        r.appendItem(QPDFObjectHandle::newReal(y1));
        return r;
    };

    QPDFObjectHandle pageDict = QPDFObjectHandle::newDictionary();
    pageDict.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle mb = QPDFObjectHandle::newArray();
    for (int v : {0, 0, 612, 792})
        mb.appendItem(QPDFObjectHandle::newInteger(v));
    pageDict.replaceKey("/MediaBox", mb);
    QPDFObjectHandle pageObj = pdf.makeIndirectObject(pageDict);

    QPDFObjectHandle tf = QPDFObjectHandle::newDictionary();
    tf.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    tf.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    tf.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    tf.replaceKey("/T", QPDFObjectHandle::newString("fullname"));
    tf.replaceKey("/V", QPDFObjectHandle::newString("Alice"));
    tf.replaceKey("/Rect", makeRect(72, 720, 288, 744));
    tf.replaceKey("/P", pageObj);
    QPDFObjectHandle tfObj = pdf.makeIndirectObject(tf);

    QPDFObjectHandle cb = QPDFObjectHandle::newDictionary();
    cb.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    cb.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    cb.replaceKey("/FT", QPDFObjectHandle::newName("/Btn"));
    cb.replaceKey("/T", QPDFObjectHandle::newString("agree"));
    cb.replaceKey("/V", QPDFObjectHandle::newName("/Yes"));
    cb.replaceKey("/AS", QPDFObjectHandle::newName("/Yes"));
    cb.replaceKey("/Rect", makeRect(72, 680, 96, 704));
    cb.replaceKey("/P", pageObj);
    QPDFObjectHandle cbObj = pdf.makeIndirectObject(cb);

    QPDFObjectHandle dd = QPDFObjectHandle::newDictionary();
    dd.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    dd.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    dd.replaceKey("/FT", QPDFObjectHandle::newName("/Ch"));
    dd.replaceKey("/Ff", QPDFObjectHandle::newInteger(131072));
    dd.replaceKey("/T", QPDFObjectHandle::newString("color"));
    dd.replaceKey("/V", QPDFObjectHandle::newString("Green"));
    QPDFObjectHandle opts = QPDFObjectHandle::newArray();
    for (const char *s : {"Red", "Green", "Blue"})
        opts.appendItem(QPDFObjectHandle::newString(s));
    dd.replaceKey("/Opt", opts);
    dd.replaceKey("/Rect", makeRect(72, 640, 288, 664));
    dd.replaceKey("/P", pageObj);
    QPDFObjectHandle ddObj = pdf.makeIndirectObject(dd);

    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(tfObj);
    annots.appendItem(cbObj);
    annots.appendItem(ddObj);
    pageDict.replaceKey("/Annots", annots);

    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageObj);
    QPDFObjectHandle pagesDict = QPDFObjectHandle::newDictionary();
    pagesDict.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    pagesDict.replaceKey("/Kids", kids);
    pagesDict.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle pagesObj = pdf.makeIndirectObject(pagesDict);
    pageDict.replaceKey("/Parent", pagesObj);

    QPDFObjectHandle acroFields = QPDFObjectHandle::newArray();
    acroFields.appendItem(tfObj);
    acroFields.appendItem(cbObj);
    acroFields.appendItem(ddObj);
    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    acroForm.replaceKey("/Fields", acroFields);
    acroForm.replaceKey("/NeedAppearances", QPDFObjectHandle::newBool(true));

    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages", pagesObj);
    root.replaceKey("/AcroForm", acroForm);

    QPDFWriter writer(pdf, path.toStdString().c_str());
    writer.write();
    return path;
}

// Sorted census-id list for a settled window. "Settled" is decided by
// the walk itself: async open/capability plumbing (offthread PDF open,
// worker-thread form detection) keeps mutating chrome for a few event-
// loop turns after openFiles() returns, so we re-walk until two
// consecutive walks kSettleGapMs apart are identical, rather than
// sleeping a guessed fixed interval (the load-sensitive-race smell,
// docs/backlog/2026-08-03-load-sensitive-offscreen-test-races.md).
//
// kSettleGapMs = 50: the gap between the two must-match walks. Range
// tried: 10 (two walks can land inside the same still-in-flight open
// and agree on a NOT-yet-final surface), 50 (stable), 200 (stable,
// just slower). Symptom to raise it: a census run intermittently
// missing chrome that a later state shows (settle declared too early).
// kSettleBudgetMs = 5000 bounds the loop so a genuinely oscillating
// surface fails loudly here instead of hanging ctest.
constexpr int kSettleGapMs = 50;
constexpr int kSettleBudgetMs = 5000;

// One walk of the at-rest surface: the window's chrome tree, PLUS an
// entry for any OTHER visible top-level widget ("toplevel:<class>") —
// the walk itself skips child windows, so a stray parentless (or
// window-flagged) surface shown at rest would otherwise be the one
// escape hatch left open. At rest, only the MainWindow should exist.
QStringList censusIds(QWidget *window) {
    QStringList ids;
    const auto elements = trailer_uat::walkChrome(window);
    for (const auto &e : elements)
        ids << e.id;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget *tl : topLevels) {
        if (tl == window || !tl->isVisible())
            continue;
        QString cls = QString::fromLatin1(tl->metaObject()->className());
        cls.remove(QStringLiteral("trailer::"));
        ids << QStringLiteral("toplevel:%1").arg(
            tl->objectName().isEmpty() ? cls : tl->objectName());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QStringList settledCensus(QWidget *window, bool *settled) {
    QElapsedTimer timer;
    timer.start();
    QStringList prev;
    bool first = true;
    while (timer.elapsed() < kSettleBudgetMs) {
        QStringList ids = censusIds(window);
        if (!first && ids == prev) {
            *settled = true;
            return ids;
        }
        prev = ids;
        first = false;
        QTest::qWait(kSettleGapMs);
    }
    *settled = false;
    return prev;
}

QJsonObject stateJson(const QStringList &menus, const QStringList &chrome) {
    QJsonObject state;
    state.insert(QStringLiteral("menus"), QJsonArray::fromStringList(menus));
    state.insert(QStringLiteral("chrome"), QJsonArray::fromStringList(chrome));
    return state;
}

QStringList jsonToStringList(const QJsonArray &arr) {
    QStringList out;
    for (const auto &v : arr)
        out << v.toString();
    return out;
}

bool regenMode() { return qEnvironmentVariableIntValue("TRAILER_CENSUS_WRITE") == 1; }

} // namespace

class TestUatChromeCensus : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    // Order is load-bearing: empty-window first (see file header).
    void uat_xct_090_emptyWindowChromeMatchesGolden();
    void uat_xct_090_pdfOpenChromeMatchesGolden();
    void uat_xct_090_imageOpenChromeMatchesGolden();
    void uat_xct_090_formPdfOpenChromeMatchesGolden();
    void uat_xct_090_probeChromeIsDetected(); // negative control

  private:
    void censusState(const QString &stateName, MainWindow *mw);
    MainWindow *openAndSettle(const QString &path);

    QTemporaryDir m_scratch;
    QJsonObject m_golden;       // full golden doc as read from disk
    QJsonObject m_observed;     // accumulated states (for regen mode)
};

void TestUatChromeCensus::initTestCase() {
    QFile f(kGoldenPath);
    if (!regenMode()) {
        QVERIFY2(f.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("cannot read the chrome-census golden at %1 — the UAT "
                                           "build bakes the source path in; run from the tree the "
                                           "build was configured in, or regenerate with "
                                           "TRAILER_CENSUS_WRITE=1. See docs/uat/06-cross-cutting.md "
                                           "(UAT-XCT-090).")
                                .arg(kGoldenPath)));
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QVERIFY2(err.error == QJsonParseError::NoError,
                 qPrintable(QStringLiteral("golden is not valid JSON: %1").arg(err.errorString())));
        m_golden = doc.object();
    }
}

void TestUatChromeCensus::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatChromeCensus::cleanupTestCase() {
    if (!regenMode())
        return;
    QJsonObject rootObj;
    rootObj.insert(
        QStringLiteral("_readme"),
        QJsonArray::fromStringList(QStringList()
            << QStringLiteral("Chrome census golden — the at-rest permanent chrome surface of "
                              "each canonical state, frozen.")
            << QStringLiteral("Compared verbatim by tests/uat/test_uat_chrome_census.cpp "
                              "(UAT-XCT-090, docs/uat/06-cross-cutting.md). A build whose at-rest "
                              "surface shows chrome not listed here fails the release gate.")
            << QStringLiteral("OWNER-ACK RULE: a change to this file merges ONLY with the owner's "
                              "explicit acknowledgment in the PR and a cited accepted decision "
                              "record in docs/decision-records/. Agents do not self-approve "
                              "golden diffs (AGENTS.md, G6/G10).")
            << QStringLiteral("The 2026-08-28 baseline is a faithful FREEZE of the surface as "
                              "built — nothing was pruned. Each row doubles as the owner's "
                              "pruning worksheet; deleting a row is an owner decision.")
            << QStringLiteral("Regenerate after an approved change: TRAILER_CENSUS_WRITE=1 "
                              "QT_QPA_PLATFORM=offscreen ./test_uat_chrome_census")));
    rootObj.insert(QStringLiteral("schema"), 1);
    rootObj.insert(QStringLiteral("states"), m_observed);
    QFile f(kGoldenPath);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("cannot write golden %1").arg(kGoldenPath)));
    f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
    qInfo().noquote() << "chrome census golden written to" << kGoldenPath;
}

// Census `mw` and compare against the golden's `stateName` section (or
// record it, in regen mode).
void TestUatChromeCensus::censusState(const QString &stateName, MainWindow *mw) {
    bool settled = false;
    const QStringList chrome = settledCensus(mw, &settled);
    QVERIFY2(settled, qPrintable(QStringLiteral(
                                     "chrome surface of state '%1' did not settle within %2ms — "
                                     "something keeps toggling at-rest chrome")
                                     .arg(stateName)
                                     .arg(kSettleBudgetMs)));
    QVERIFY2(!chrome.isEmpty(), "census walk found no chrome at all — window not realized?");
    const QStringList menus = trailer_uat::menuTitles(mw->menuBar());
    QVERIFY2(!menus.isEmpty(), "menu census found no top-level menus — menu bar not built?");

    if (regenMode()) {
        m_observed.insert(stateName, stateJson(menus, chrome));
        return;
    }

    const QJsonObject states = m_golden.value(QStringLiteral("states")).toObject();
    // Reverse containment: every state in the golden must be one this
    // suite still censuses, or a renamed canonical state would leave a
    // stale, never-compared section in the golden forever (correctness
    // review, 2026-08-28). Checked against the static canonical set, not
    // m_observed, so a macOS run (which QSKIPs empty-window per DESIGN
    // §2.4.2) doesn't false-alarm. NOTE regen footgun: regenerating on
    // macOS writes a golden MISSING empty-window that then fails on every
    // Linux run — regenerate goldens on Linux only.
    static const QSet<QString> kCanonicalStates = {
        QStringLiteral("empty-window"), QStringLiteral("pdf-open"),
        QStringLiteral("image-open"), QStringLiteral("form-pdf-open")};
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        QVERIFY2(kCanonicalStates.contains(it.key()),
                 qPrintable(QStringLiteral("golden holds unknown state '%1' — stale section "
                                           "from a renamed canonical state; regenerate on Linux "
                                           "(TRAILER_CENSUS_WRITE=1, owner-acknowledged PR)")
                                .arg(it.key())));
    }
    QVERIFY2(states.contains(stateName),
             qPrintable(QStringLiteral("golden has no state '%1' — regenerate the golden "
                                       "(TRAILER_CENSUS_WRITE=1) in an owner-acknowledged PR")
                            .arg(stateName)));
    const QJsonObject goldenState = states.value(stateName).toObject();
    const QStringList goldenChrome =
        jsonToStringList(goldenState.value(QStringLiteral("chrome")).toArray());
    const QStringList goldenMenus =
        jsonToStringList(goldenState.value(QStringLiteral("menus")).toArray());

    QStringList added, missing;
    for (const QString &id : chrome)
        if (!goldenChrome.contains(id))
            added << id;
    for (const QString &id : goldenChrome)
        if (!chrome.contains(id))
            missing << id;

    const QString verdict = QStringLiteral(
        "chrome census MISMATCH in state '%1'.\n"
        "  NEW chrome not in the golden (fails the deference ratchet — new permanent chrome "
        "needs an accepted decision record + explicit owner ack on the golden diff):\n    %2\n"
        "  Chrome in the golden but absent from the build (removal also needs the owner-ack "
        "golden diff — and is usually good news):\n    %3\n"
        "  Fix: if this chrome change is intended and owner-approved, regenerate with "
        "TRAILER_CENSUS_WRITE=1 and commit docs/uat/chrome-census.json in the same PR. "
        "See docs/uat/06-cross-cutting.md (UAT-XCT-090) and AGENTS.md gate G10.")
        .arg(stateName,
             added.isEmpty() ? QStringLiteral("(none)") : added.join(QStringLiteral("\n    ")),
             missing.isEmpty() ? QStringLiteral("(none)") : missing.join(QStringLiteral("\n    ")));
    QVERIFY2(added.isEmpty() && missing.isEmpty(), qPrintable(verdict));

    QVERIFY2(menus == goldenMenus,
             qPrintable(QStringLiteral("top-level menu census MISMATCH in state '%1'.\n"
                                       "  build: %2\n  golden: %3\n"
                                       "  Menu additions/removals/reorders take the same "
                                       "owner-acked golden diff as widget chrome.")
                            .arg(stateName, menus.join(QStringLiteral(" | ")),
                                 goldenMenus.join(QStringLiteral(" | ")))));
}

MainWindow *TestUatChromeCensus::openAndSettle(const QString &path) {
    auto *app = qobject_cast<Application *>(qApp);
    if (!app)
        return nullptr;
    app->openFiles({path});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    if (!mw)
        return nullptr;
    mw->resize(1100, 750);
    mw->show();
    QApplication::processEvents();
    return mw;
}

void TestUatChromeCensus::uat_xct_090_emptyWindowChromeMatchesGolden() {
#ifdef Q_OS_MACOS
    QSKIP("macOS shows no window in the empty state (DESIGN §2.4.2); nothing to census.");
#else
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    mw->show();
    QApplication::processEvents();
    QCOMPARE(mw->documentCount(), 0);
    censusState(QStringLiteral("empty-window"), mw);
#endif
}

void TestUatChromeCensus::uat_xct_090_pdfOpenChromeMatchesGolden() {
    QVERIFY(m_scratch.isValid());
    MainWindow *mw =
        openAndSettle(writeTinyPdf(m_scratch.filePath(QStringLiteral("census.pdf"))));
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QTRY_VERIFY(dv->currentDocument() != nullptr);
    censusState(QStringLiteral("pdf-open"), mw);
}

void TestUatChromeCensus::uat_xct_090_imageOpenChromeMatchesGolden() {
    QVERIFY(m_scratch.isValid());
    MainWindow *mw =
        openAndSettle(writeStaticImage(m_scratch.filePath(QStringLiteral("census.png"))));
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QTRY_VERIFY(dv->currentDocument() != nullptr);
    censusState(QStringLiteral("image-open"), mw);
}

void TestUatChromeCensus::uat_xct_090_formPdfOpenChromeMatchesGolden() {
    QVERIFY(m_scratch.isValid());
    MainWindow *mw =
        openAndSettle(writeFormPdf(m_scratch.filePath(QStringLiteral("census_form.pdf"))));
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QTRY_VERIFY(dv->currentDocument() != nullptr);
    // Async form detection (PR #63) drives the form-state chrome; wait
    // for the capability itself, then let settledCensus() confirm the
    // chrome that follows from it has stopped changing.
    QTRY_VERIFY(dv->currentDocument()->supportsFormFilling());
    censusState(QStringLiteral("form-pdf-open"), mw);
}

// Negative control: the census oracle must actually detect a fresh
// permanent chrome element. Inject a probe QLabel into the status bar
// (the classic accretion vector) and assert the walk reports an id the
// golden cannot contain. Skipped in regen mode — the probe must never
// be frozen into a golden.
void TestUatChromeCensus::uat_xct_090_probeChromeIsDetected() {
    if (regenMode())
        QSKIP("regen mode: the probe must not be recorded into the golden");
    QVERIFY(m_scratch.isValid());
    MainWindow *mw =
        openAndSettle(writeTinyPdf(m_scratch.filePath(QStringLiteral("census_probe.pdf"))));
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QTRY_VERIFY(dv->currentDocument() != nullptr);

    auto *probe = new QLabel(QStringLiteral("probe"), mw);
    probe->setObjectName(QStringLiteral("censusProbeLabel"));
    mw->statusBar()->addPermanentWidget(probe);
    QApplication::processEvents();

    bool settled = false;
    const QStringList chrome = settledCensus(mw, &settled);
    QVERIFY(settled);
    bool found = false;
    for (const QString &id : chrome)
        if (id.endsWith(QStringLiteral("censusProbeLabel")))
            found = true;
    // Clean up before asserting so a failure doesn't leak the probe
    // into any later state.
    mw->statusBar()->removeWidget(probe);
    probe->deleteLater();
    QApplication::processEvents();
    QVERIFY2(found, "census walk failed to see a QLabel added to the status bar — the oracle "
                    "is blind and every census verdict above is unsound");

    const QJsonObject states = m_golden.value(QStringLiteral("states")).toObject();
    const QStringList goldenChrome = jsonToStringList(
        states.value(QStringLiteral("pdf-open")).toObject().value(QStringLiteral("chrome")).toArray());
    for (const QString &id : goldenChrome)
        QVERIFY2(!id.contains(QStringLiteral("censusProbeLabel")),
                 "the probe leaked into the committed golden — regenerate it without the probe");
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir (and the
// empty-window census sees an empty Recent list — see file header).
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
    TestUatChromeCensus tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_chrome_census.moc"
