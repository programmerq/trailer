// UAT harness — Modal census (UAT-XCT-091/092, docs/uat/06-cross-cutting.md).
//
// Two halves, per the cohesion-mechanisms plan ("no new modal reachable
// from the read path"):
//
//   STATIC (UAT-XCT-091): scan every source file under src/ for modal
//   call sites — argumentless .exec()/->exec() calls (dialogs, message
//   boxes, progress dialogs, event loops) and the static modal entry
//   points (QMessageBox::warning & friends, QInputDialog::get*,
//   QFileDialog::get*, QColorDialog::getColor, QFontDialog::getFont) —
//   and diff the per-file, per-kind COUNTS against the committed golden
//   docs/uat/modal-census.json. A new modal call site anywhere under
//   src/ fails until the golden changes, and the golden changes only
//   with explicit owner acknowledgment + an accepted decision record.
//
//   Counts per (file, kind) rather than file:line site entries — a
//   deliberate trade: line numbers (and message texts) churn on every
//   unrelated edit, which would make the owner-acked golden ring
//   constantly and teach everyone to rubber-stamp it. Counts stay
//   silent under refactors and wording tweaks, and ring exactly when a
//   modal call is added or removed. Scope notes: QMenu::exec(pos)
//   (transient popup, takes an argument) is deliberately out;
//   QEventLoop::exec() and qApp->exec() (not modal UI, but
//   grep-indistinguishable from dialog exec()) are deliberately IN —
//   the golden freezes them alongside the true modals rather than
//   risking a matcher hole.
//
//   DYNAMIC (UAT-XCT-092): drive the read path — open, page through,
//   zoom, search, dismiss search, close — against a real PDF offscreen
//   with a sentinel ticking on the event loop, and assert
//   QApplication::activeModalWidget() stays null throughout. The
//   sentinel runs INSIDE any nested modal event loop too (timers keep
//   firing there), so a modal that would hang the harness is instead
//   closed, recorded, and reported as a failure.
//
// GOLDEN OWNER-ACK RULE (also in the golden's _readme): a diff to
// docs/uat/modal-census.json merges only with explicit owner
// acknowledgment and a cited accepted decision record. The 2026-08-28
// baseline is a faithful freeze; pruning is a later owner decision.
//
// Regenerate after an owner-approved modal-surface change:
//   TRAILER_CENSUS_WRITE=1 QT_QPA_PLATFORM=offscreen ./test_uat_modal_census

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
#error "TRAILER_CENSUS_GOLDEN_DIR must point at <source>/docs/uat (set in tests/uat/CMakeLists.txt)"
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
    QDirIterator it(kSrcDir, {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                              QStringLiteral("*.hpp"), QStringLiteral("*.mm")},
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

bool regenMode() { return qEnvironmentVariableIntValue("TRAILER_CENSUS_WRITE") == 1; }

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
    const QString probe = QStringLiteral(
        "void f() {\n"
        "    QMessageBox::warning(this, tr(\"t\"), tr(\"b\"));\n"
        "    // QMessageBox::critical(this, a, b);  <- comment, not a site\n"
        "    /* QInputDialog::getText(this, a, b); */\n"
        "    const QString u = \"https://example.com/not-a-comment\"; box.exec();\n"
        "    QMessageBox::information(\n"
        "        this, tr(\"split across lines\"), tr(\"still one site\"));\n"
        "    loop->exec();\n"
        "    menu.exec(QCursor::pos()); // popup with args: out of scope\n"
        "}\n");
    const auto counts = countModalSites(stripComments(probe));
    QVERIFY(counts.count(QStringLiteral("QMessageBox-static")) != 0);
    QCOMPARE(counts.at(QStringLiteral("QMessageBox-static")), 2);
    QVERIFY(counts.count(QStringLiteral("QInputDialog-static")) == 0);
    QVERIFY(counts.count(QStringLiteral("exec")) != 0);
    QCOMPARE(counts.at(QStringLiteral("exec")), 2);
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
            QJsonArray::fromStringList(QStringList()
                << QStringLiteral("Modal census golden — every modal call site under src/, as "
                                  "per-file, per-kind counts, frozen.")
                << QStringLiteral("Compared verbatim by tests/uat/test_uat_modal_census.cpp "
                                  "(UAT-XCT-091, docs/uat/06-cross-cutting.md). A new "
                                  "exec()/QMessageBox-static/QInputDialog/QFileDialog/QColorDialog/"
                                  "QFontDialog call site anywhere under src/ fails the release "
                                  "gate until this file changes.")
                << QStringLiteral("OWNER-ACK RULE: a change to this file merges ONLY with the "
                                  "owner's explicit acknowledgment in the PR and a cited accepted "
                                  "decision record in docs/decision-records/. Agents do not "
                                  "self-approve golden diffs (AGENTS.md, G6/G10).")
                << QStringLiteral("Scope: argumentless .exec()/->exec() (dialogs, boxes, progress "
                                  "dialogs — and, indistinguishably by grep, event loops and "
                                  "qApp->exec(), frozen alongside rather than risking a matcher "
                                  "hole). QMenu::exec(pos) is a transient popup and is out of "
                                  "scope. The 2026-08-28 baseline is a faithful FREEZE — nothing "
                                  "was pruned; each row is the owner's pruning worksheet.")
                << QStringLiteral("Regenerate after an approved change: TRAILER_CENSUS_WRITE=1 "
                                  "QT_QPA_PLATFORM=offscreen ./test_uat_modal_census")));
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
             qPrintable(QStringLiteral("cannot read the modal-census golden at %1 — regenerate "
                                       "with TRAILER_CENSUS_WRITE=1. See "
                                       "docs/uat/06-cross-cutting.md (UAT-XCT-091).")
                            .arg(kGoldenPath)));
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    QVERIFY2(err.error == QJsonParseError::NoError,
             qPrintable(QStringLiteral("golden is not valid JSON: %1").arg(err.errorString())));
    const auto golden =
        jsonToCensus(doc.object().value(QStringLiteral("sites")).toObject());

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
        if (census.count(file))
            continue;
        for (const auto &[kind, count] : kinds)
            diffs << QStringLiteral("%1: golden expects %2 site(s) of %3, build has none — "
                                    "removed or renamed file; ratchet the golden down")
                         .arg(file)
                         .arg(count)
                         .arg(kind);
    }

    QVERIFY2(diffs.isEmpty(),
             qPrintable(QStringLiteral(
                            "modal census MISMATCH:\n  %1\n"
                            "  A modal surface change needs an accepted decision record and the "
                            "owner's explicit ack on the golden diff; then regenerate with "
                            "TRAILER_CENSUS_WRITE=1 and commit docs/uat/modal-census.json in the "
                            "same PR. Prefer a non-modal shape first — disable-with-tooltip, "
                            "banner, status-slot hint (PHILOSOPHY: no popup that just says no). "
                            "See docs/uat/06-cross-cutting.md (UAT-XCT-091) and AGENTS.md G10.")
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

    // Close the (clean, titled) document — never prompts (ADR-0004 keeps
    // prompts for dirty/untitled docs only, and a prompt here would be
    // caught as a failure below anyway).
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();
    QVERIFY2(QApplication::activeModalWidget() == nullptr, "modal after close");

    QVERIFY2(sentinel.seen().isEmpty(),
             qPrintable(QStringLiteral(
                            "a modal widget surfaced on the read path (closed by the sentinel so "
                            "the harness could report instead of hanging): %1\n"
                            "  The read path is modal-free by design — see docs/uat/"
                            "06-cross-cutting.md (UAT-XCT-092) and PHILOSOPHY (no popup that "
                            "just says no).")
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
