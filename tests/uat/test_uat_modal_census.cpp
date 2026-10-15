// UAT harness — Modal census (UAT-XCT-091/092).
//
// PRODUCT.md allows popups only for decisions: "picking a file or page
// range, confirming irreversible loss, one-time consent (a model
// download), and errors the UI can't make obvious." Two halves keep the
// modal surface visible and the read path modal-free:
//
//   STATIC (UAT-XCT-091): scan every source file under src/ for modal
//   call sites — argumentless .exec()/->exec() calls (dialogs, message
//   boxes, progress dialogs, event loops), the static modal entry
//   points (QMessageBox::warning & friends, QInputDialog::get*,
//   QFileDialog::get*, QColorDialog::getColor, QFontDialog::getFont), and
//   dialogs made modal without exec() (setWindowModality(Qt::WindowModal
//   or Qt::ApplicationModal), setModal(true), QDialog::open()) —
//   and diff the per-file, per-kind COUNTS against the committed golden
//   tests/uat/census/modal-census.json. A modal call site added or
//   removed anywhere under src/ fails until the golden is regenerated,
//   so every change to the modal surface shows up as a golden diff in
//   its PR.
//
//   Counts per (file, kind) rather than file:line site entries — a
//   deliberate trade: line numbers (and message texts) churn on every
//   unrelated edit, which would make the golden ring constantly and
//   teach everyone to rubber-stamp it. Counts stay silent under
//   refactors and wording tweaks, and ring exactly when a modal call is
//   added or removed. Scope notes: QMenu::exec(pos) (transient popup,
//   takes an argument) is deliberately out; QEventLoop::exec() and
//   qApp->exec() (not modal UI, but grep-indistinguishable from dialog
//   exec()) are deliberately IN — the golden counts them alongside the
//   true modals rather than risking a matcher hole.
//
//   DYNAMIC (UAT-XCT-092): drive the read path — open, page through,
//   zoom, search, dismiss search, close — against a real PDF offscreen
//   with a sentinel ticking on the event loop, and assert
//   QApplication::activeModalWidget() stays null throughout. The
//   sentinel runs INSIDE any nested modal event loop too (timers keep
//   firing there), so a modal that would hang the harness is instead
//   closed, recorded, and reported as a failure.
//
// Regenerate after a modal call site is added or removed:
//   TRAILER_CENSUS_WRITE=1 QT_QPA_PLATFORM=offscreen ./test_uat_modal_census
// (writes the golden in place under the source tree; include the golden
// diff in the PR so the reviewer sees what changed.)

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/SearchBar.h"

#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

#include <map>

using namespace trailer;

#ifndef TRAILER_CENSUS_GOLDEN_DIR
#error "TRAILER_CENSUS_GOLDEN_DIR must point at tests/uat/census (set in tests/uat/CMakeLists.txt)"
#endif
#ifndef TRAILER_SOURCE_SRC_DIR
#error "TRAILER_SOURCE_SRC_DIR must point at <source>/src (set in tests/uat/CMakeLists.txt)"
#endif

namespace {

const QString kGoldenPath =
    QStringLiteral(TRAILER_CENSUS_GOLDEN_DIR) + QStringLiteral("/modal-census.json");
const QString kSrcDir = QStringLiteral(TRAILER_SOURCE_SRC_DIR);

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QString writeThreePagePdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("Modal census fixture page one"));
    writer.newPage();
    p.drawText(100, 100, QStringLiteral("page two"));
    writer.newPage();
    p.drawText(100, 100, QStringLiteral("page three"));
    p.end();
    return path;
}

// Strip // and /* */ comments while leaving string/char literal contents
// intact (so a "//" inside a URL string does not eat the rest of the
// line, and a "/*" inside a string does not eat the rest of the file).
// Raw string literals are not specially handled — none of the matched
// tokens appear inside raw strings in this codebase, and a raw string
// would at worst make the matcher OVER-count (fail loud), never
// under-count silently.
QString stripComments(const QString &in) {
    QString out;
    out.reserve(in.size());
    enum State { Code, LineComment, BlockComment, DQString, SQChar } state = Code;
    for (int i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        const QChar next = (i + 1 < in.size()) ? in.at(i + 1) : QChar();
        switch (state) {
        case Code:
            if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
                state = LineComment;
                ++i;
            } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
                state = BlockComment;
                ++i;
            } else {
                if (c == QLatin1Char('"'))
                    state = DQString;
                else if (c == QLatin1Char('\''))
                    state = SQChar;
                out.append(c);
            }
            break;
        case LineComment:
            if (c == QLatin1Char('\n')) {
                state = Code;
                out.append(c);
            }
            break;
        case BlockComment:
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = Code;
                ++i;
            } else if (c == QLatin1Char('\n')) {
                out.append(c); // keep line structure for readable diffs
            }
            break;
        case DQString:
            out.append(c);
            if (c == QLatin1Char('\\') && !next.isNull()) {
                out.append(next);
                ++i;
            } else if (c == QLatin1Char('"')) {
                state = Code;
            }
            break;
        case SQChar:
            out.append(c);
            if (c == QLatin1Char('\\') && !next.isNull()) {
                out.append(next);
                ++i;
            } else if (c == QLatin1Char('\'')) {
                state = Code;
            }
            break;
        }
    }
    return out;
}

struct ModalMatcher {
    QString kind;
    QRegularExpression re;
};

// The matchers run over comment-stripped whole-file text, so calls
// split across lines by clang-format still match (the class::method
// token itself is never split in this codebase's style).
const QList<ModalMatcher> &modalMatchers() {
    static const QList<ModalMatcher> matchers = {
        {QStringLiteral("QMessageBox-static"),
         QRegularExpression(QStringLiteral(
             "QMessageBox::(information|warning|critical|question|about|aboutQt)\\s*\\("))},
        {QStringLiteral("QInputDialog-static"),
         QRegularExpression(QStringLiteral("QInputDialog::get[A-Za-z]+\\s*\\("))},
        {QStringLiteral("QFileDialog-static"),
         QRegularExpression(QStringLiteral("QFileDialog::get[A-Za-z]+\\s*\\("))},
        {QStringLiteral("QColorDialog-static"),
         QRegularExpression(QStringLiteral("QColorDialog::getColor\\s*\\("))},
        {QStringLiteral("QFontDialog-static"),
         QRegularExpression(QStringLiteral("QFontDialog::getFont\\s*\\("))},
        // Argumentless member exec() — dialogs, boxes, progress dialogs,
        // event loops, qApp. QMenu::exec(pos) takes arguments and is a
        // transient popup, not a modal dialog; see file header.
        {QStringLiteral("exec"),
         QRegularExpression(QStringLiteral("(\\.|->)\\s*exec\\s*\\(\\s*\\)"))},
        // Modal without exec(): a dialog made modal and then shown (a
        // QProgressDialog shows itself), or opened with QDialog::open(). A
        // dialog made modal and then exec()'d counts under both kinds.
        {QStringLiteral("setWindowModality"),
         QRegularExpression(QStringLiteral("setWindowModality\\s*\\(\\s*Qt::(WindowModality::)?"
                                           "(WindowModal|ApplicationModal)\\s*\\)"))},
        {QStringLiteral("setModal"),
         QRegularExpression(QStringLiteral("setModal\\s*\\(\\s*true\\s*\\)"))},
        // QDialog::open() returns void, so it stands alone as a statement,
        // while QTemporaryFile::open() returns a bool the caller tests. The
        // receiver/member overload (QFileDialog::open(this, SLOT(...)) and
        // the other dialogs') takes the receiver first.
        {QStringLiteral("open"),
         QRegularExpression(QStringLiteral("(\\.|->)\\s*open\\s*\\(\\s*(\\)\\s*;|this\\s*,)"))},
    };
    return matchers;
}

// kind -> count for one file's (comment-stripped) text.
std::map<QString, int> countModalSites(const QString &strippedText) {
    std::map<QString, int> counts;
    for (const ModalMatcher &m : modalMatchers()) {
        int n = 0;
        auto it = m.re.globalMatch(strippedText);
        while (it.hasNext()) {
            it.next();
            ++n;
        }
        if (n > 0)
            counts[m.kind] = n;
    }
    return counts;
}

// file (relative, '/'-separated) -> kind -> count, for all of src/.
std::map<QString, std::map<QString, int>> scanSourceTree(int *filesScanned) {
    std::map<QString, std::map<QString, int>> result;
    *filesScanned = 0;
    QDirIterator it(kSrcDir,
                    {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"),
                     QStringLiteral("*.mm")},
                    QDir::Files, QDirIterator::Subdirectories);
    const QDir root(kSrcDir);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        ++(*filesScanned);
        const auto counts = countModalSites(stripComments(QString::fromUtf8(f.readAll())));
        if (!counts.empty())
            result[QStringLiteral("src/") + root.relativeFilePath(path)] = counts;
    }
    return result;
}

QJsonObject censusToJson(const std::map<QString, std::map<QString, int>> &census) {
    QJsonObject files;
    for (const auto &[file, kinds] : census) {
        QJsonObject k;
        for (const auto &[kind, count] : kinds)
            k.insert(kind, count);
        files.insert(file, k);
    }
    return files;
}

std::map<QString, std::map<QString, int>> jsonToCensus(const QJsonObject &files) {
    std::map<QString, std::map<QString, int>> out;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject kinds = it.value().toObject();
        std::map<QString, int> k;
        for (auto kit = kinds.begin(); kit != kinds.end(); ++kit)
            k[kit.key()] = kit.value().toInt();
        out[it.key()] = k;
    }
    return out;
}

bool regenMode() {
    return qEnvironmentVariableIntValue("TRAILER_CENSUS_WRITE") == 1;
}

// Watches for any active modal widget from the running event loop —
// including a NESTED modal loop, where QTimer still fires — records it,
// and closes it so the harness reports a failure instead of hanging.
class ModalSentinel : public QObject {
  public:
    ModalSentinel() {
        // 25ms: fast enough that a modal opened and awaiting input is
        // seen and dismissed well inside the test's own waits; the
        // exact value is uncritical because a modal exec() BLOCKS until
        // the sentinel closes it — the tick just has to happen at all.
        m_timer.setInterval(25);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this]() {
            if (QWidget *m = QApplication::activeModalWidget()) {
                m_seen << QString::fromLatin1(m->metaObject()->className());
                m->close();
            }
        });
        m_timer.start();
    }
    QStringList seen() const { return m_seen; }

  private:
    QTimer m_timer;
    QStringList m_seen;
};

} // namespace

class TestUatModalCensus : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_xct_091_matcherNegativeControl();
    void uat_xct_091_staticModalSitesMatchGolden();
    void uat_xct_092_readPathStaysModalFree();

  private:
    QTemporaryDir m_scratch;
};

void TestUatModalCensus::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// Negative control: the matcher must count real call sites and must NOT
// count commented-out ones — otherwise every verdict below is unsound.
void TestUatModalCensus::uat_xct_091_matcherNegativeControl() {
    const QString probe =
        QStringLiteral("void f() {\n"
                       "    QMessageBox::warning(this, tr(\"t\"), tr(\"b\"));\n"
                       "    // QMessageBox::critical(this, a, b);  <- comment, not a site\n"
                       "    /* QInputDialog::getText(this, a, b); */\n"
                       "    const QString u = \"https://example.com/not-a-comment\"; box.exec();\n"
                       "    QMessageBox::information(\n"
                       "        this, tr(\"split across lines\"), tr(\"still one site\"));\n"
                       "    loop->exec();\n"
                       "    menu.exec(QCursor::pos()); // popup with args: out of scope\n"
                       "    progress.setWindowModality(Qt::WindowModal);\n"
                       "    sheet->setWindowModality(Qt::NonModal);\n"
                       "    // dialog.setWindowModality(Qt::ApplicationModal);\n"
                       "    setModal(true);\n"
                       "    setModal(false);\n"
                       "    picker->open();\n"
                       "    chooser.open(this, SLOT(onChosen()));\n"
                       "    if (!scratch.open()) // a QTemporaryFile, tested: not a site\n"
                       "        return;\n"
                       "    log.open(QIODevice::WriteOnly);\n"
                       "}\n");
    const auto counts = countModalSites(stripComments(probe));
    const auto countOf = [&counts](const char *kind) {
        const auto it = counts.find(QString::fromLatin1(kind));
        return it == counts.end() ? 0 : it->second;
    };
    QCOMPARE(countOf("QMessageBox-static"), 2);
    QCOMPARE(countOf("QInputDialog-static"), 0);
    QCOMPARE(countOf("exec"), 2);
    QCOMPARE(countOf("setWindowModality"), 1);
    QCOMPARE(countOf("setModal"), 1);
    QCOMPARE(countOf("open"), 2);
}

void TestUatModalCensus::uat_xct_091_staticModalSitesMatchGolden() {
    int filesScanned = 0;
    const auto census = scanSourceTree(&filesScanned);
    QVERIFY2(filesScanned > 50,
             qPrintable(QStringLiteral("only %1 source files scanned under %2 — the baked source "
                                       "path is stale? (build tree moved after configure)")
                            .arg(filesScanned)
                            .arg(kSrcDir)));
    QVERIFY2(!census.empty(), "scan found zero modal sites in the whole of src/ — matcher broken");

    if (regenMode()) {
        QJsonObject rootObj;
        rootObj.insert(
            QStringLiteral("_readme"),
            QJsonArray::fromStringList(
                QStringList()
                << QStringLiteral("Modal census golden: every modal call site under src/, as "
                                  "per-file, per-kind counts, compared verbatim by "
                                  "tests/uat/test_uat_modal_census.cpp (UAT-XCT-091).")
                << QStringLiteral("Counted: argumentless .exec()/->exec() (dialogs, message "
                                  "boxes, progress dialogs, and the event loops and qApp->exec() "
                                  "that grep can't tell apart from them), the static "
                                  "QMessageBox/QInputDialog/QFileDialog/QColorDialog/QFontDialog "
                                  "helpers, and dialogs made modal without exec(): "
                                  "setWindowModality(Qt::WindowModal or Qt::ApplicationModal), "
                                  "setModal(true) and QDialog::open(). A dialog made modal and "
                                  "then exec()'d counts under both kinds. QMenu::exec(pos) is a "
                                  "transient popup and isn't counted.")
                << QStringLiteral("To update, regenerate: TRAILER_CENSUS_WRITE=1 "
                                  "QT_QPA_PLATFORM=offscreen ./test_uat_modal_census, and show "
                                  "this file's diff in the PR so the reviewer sees what "
                                  "changed.")));
        rootObj.insert(QStringLiteral("schema"), 1);
        rootObj.insert(QStringLiteral("sites"), censusToJson(census));
        QFile f(kGoldenPath);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 qPrintable(QStringLiteral("cannot write golden %1").arg(kGoldenPath)));
        f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
        qInfo().noquote() << "modal census golden written to" << kGoldenPath;
        return;
    }

    QFile f(kGoldenPath);
    QVERIFY2(f.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("cannot read the modal-census golden at %1 — the build "
                                       "bakes the source path in; run from the tree the build "
                                       "was configured in, or regenerate with "
                                       "TRAILER_CENSUS_WRITE=1 and include the golden diff in "
                                       "the PR.")
                            .arg(kGoldenPath)));
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    QVERIFY2(err.error == QJsonParseError::NoError,
             qPrintable(QStringLiteral("golden is not valid JSON: %1").arg(err.errorString())));
    const auto golden = jsonToCensus(doc.object().value(QStringLiteral("sites")).toObject());

    // Matcher scope: exec(), the static helpers, and modality set with
    // setWindowModality()/setModal(true) or opened with QDialog::open().
    // A modal made any other way (Qt::WA_ShowModal, a modality passed in
    // a variable, open(receiver, member) with a receiver other than this,
    // an exec() or open() called without a receiver from inside the
    // dialog's own class) is NOT counted; the dynamic read-path sentinel
    // (UAT-XCT-092) is the only guard for those. Extend the matcher before
    // src/ grows such a site.
    QStringList diffs;
    for (const auto &[file, kinds] : census) {
        const auto git = golden.find(file);
        for (const auto &[kind, count] : kinds) {
            const int goldenCount =
                (git == golden.end()) ? 0 : (git->second.count(kind) ? git->second.at(kind) : 0);
            if (count > goldenCount)
                diffs << QStringLiteral("%1: %2 site(s) of %3, golden allows %4 — a NEW modal "
                                        "call site")
                             .arg(file)
                             .arg(count)
                             .arg(kind)
                             .arg(goldenCount);
            else if (count < goldenCount)
                diffs << QStringLiteral("%1: %2 site(s) of %3, golden expects %4 — a modal was "
                                        "REMOVED (good news, but the golden must ratchet down "
                                        "with it)")
                             .arg(file)
                             .arg(count)
                             .arg(kind)
                             .arg(goldenCount);
        }
    }
    for (const auto &[file, kinds] : golden) {
        const auto cit = census.find(file);
        for (const auto &[kind, count] : kinds) {
            // Covers BOTH the whole-file-gone case and the kind-gone case
            // (a kind whose live count dropped to zero in a file that
            // still has other kinds — countModalSites omits zero counts,
            // so the forward loop never visits it and the ratchet would
            // silently go stale).
            const bool kindGone = cit == census.end() || cit->second.count(kind) == 0;
            if (kindGone)
                diffs << QStringLiteral("%1: golden expects %2 site(s) of %3, build has none — "
                                        "removed site or renamed file; ratchet the golden down")
                             .arg(file)
                             .arg(count)
                             .arg(kind);
        }
    }

    QVERIFY2(diffs.isEmpty(),
             qPrintable(QStringLiteral(
                            "modal census MISMATCH:\n  %1\n"
                            "  PRODUCT.md allows a popup only for a decision: \"picking a file "
                            "or page range, confirming irreversible loss, one-time consent (a "
                            "model download), and errors the UI can't make obvious.\" If a new "
                            "modal is none of these, show its state in context instead (a "
                            "disabled control whose tooltip says why, a banner). If it is one of "
                            "these, or a modal was removed, regenerate with "
                            "TRAILER_CENSUS_WRITE=1 and include the golden diff "
                            "(tests/uat/census/modal-census.json) in the PR.")
                            .arg(diffs.join(QStringLiteral("\n  ")))));
}

// UAT-XCT-092 — the read path (open, page through, zoom, search,
// dismiss, close) never surfaces a modal widget.
void TestUatModalCensus::uat_xct_092_readPathStaysModalFree() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeThreePagePdf(m_scratch.filePath(QStringLiteral("readpath.pdf")));

    ModalSentinel sentinel;
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Open.
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    mw->show();
    QApplication::processEvents();
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QTRY_VERIFY(dv->currentDocument() != nullptr);
    QTRY_COMPARE(dv->currentDocument()->pageCount(), 3);
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal after open");

    auto actionByName = [mw](const char *name) {
        return mw->findChild<QAction *>(QString::fromLatin1(name));
    };

    // Page through (the scroll step of the read path).
    QAction *nextPage = actionByName("action.view.nextPage");
    QVERIFY2(nextPage, "action.view.nextPage not found");
    nextPage->trigger();
    QApplication::processEvents();
    nextPage->trigger();
    QApplication::processEvents();
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal after paging");

    // Zoom.
    QAction *zoomIn = actionByName("action.view.zoomIn");
    QVERIFY2(zoomIn, "action.view.zoomIn not found");
    zoomIn->trigger();
    QApplication::processEvents();
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal after zoom");

    // Search: reveal the bar via the Find action, type a query that has
    // a real match, then dismiss with Escape.
    QAction *find = actionByName("action.edit.find");
    QVERIFY2(find, "action.edit.find not found");
    find->trigger();
    QApplication::processEvents();
    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    QTRY_VERIFY(searchBar->isVisible());
    auto *input = searchBar->findChild<QLineEdit *>();
    QVERIFY(input);
    QTest::keyClicks(input, QStringLiteral("page"));
    // Let the async search model + the 150ms match-count poll run.
    QTest::qWait(400);
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal during search");
    QTest::keyClick(input, Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal after search dismiss");

    // Close the (clean, titled) document — never prompts (PRODUCT.md: a
    // clean document never asks; a prompt here would be caught as a
    // failure below anyway).
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal after close");

    QVERIFY2(sentinel.seen().isEmpty(),
             qPrintable(QStringLiteral(
                            "a modal widget surfaced on the read path (closed by the sentinel so "
                            "the harness could report instead of hanging): %1\n"
                            "  Opening, paging, zooming, searching and closing a clean document "
                            "decide nothing, and PRODUCT.md allows a popup only for a decision "
                            "(\"picking a file or page range, confirming irreversible loss, "
                            "one-time consent (a model download), and errors the UI can't make "
                            "obvious\"): show this state in context instead.")
                            .arg(sentinel.seen().join(QStringLiteral(", ")))));
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
    TestUatModalCensus tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_modal_census.moc"
