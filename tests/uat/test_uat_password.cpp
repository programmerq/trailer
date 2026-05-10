// UAT harness — Password-protected PDF (Phase 5)
//
// Covers the lock/unlock open flow and the export-with-password round
// trip. Cases map to the manual checklist in docs/uat/07-security.md
// (which we create in this same commit). Each slot is named after the
// spec ID it exercises so a failing test points straight at the prose.
//
//   uat_sec_010_openLockedPdfPrompts
//       Opening a password-protected PDF triggers the installed
//       prompt hook; supplying the right password loads the document.
//
//   uat_sec_011_wrongPasswordKeepsLocked
//       Three wrong passwords cause the adapter to give up; the
//       document stays locked (needsPassword stays true).
//
//   uat_sec_012_cancelPasswordAborts
//       The prompt returning nullopt skips further attempts and
//       returns a locked document rather than crashing.
//
//   uat_sec_020_exportWithPasswordRoundTrip
//       File > Export as Password-Protected PDF… writes an AES-256
//       encrypted PDF that is unreadable without the right password
//       and fully readable with it.

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
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

// Writes a minimal one-page PDF to `path`.
QString writeSample(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(200, 400, QStringLiteral("UAT security fixture"));
    p.end();
    return path;
}

// Writes a password-protected one-page PDF to `path` using
// PdfEditor directly (bypasses any UI dialogs).
QString writeLockedPdf(const QString &path, const QString &password) {
    const QString tmpSrc = path + QStringLiteral(".plain.tmp");
    writeSample(tmpSrc);
    PdfEditor editor;
    editor.load(tmpSrc);
    EncryptionOptions enc;
    enc.userPassword = password;
    editor.save(path, enc);
    QFile::remove(tmpSrc);
    return path;
}

// RAII guard that installs a prompt shim and restores the default on
// destruction. The shim delivers passwords from a queue, then returns
// nullopt to simulate "user cancelled" once the queue is exhausted.
class PromptGuard {
  public:
    explicit PromptGuard(QStringList passwords) : m_passwords(std::move(passwords)) {
        PdfAdapter::setPasswordPrompt([this](const QString &, int) -> std::optional<QString> {
            if (m_passwords.isEmpty())
                return std::nullopt;
            return m_passwords.takeFirst();
        });
    }
    ~PromptGuard() { PdfAdapter::setPasswordPrompt({}); }
    PromptGuard(const PromptGuard &) = delete;
    PromptGuard &operator=(const PromptGuard &) = delete;

  private:
    QStringList m_passwords;
};

} // namespace

class TestUatPassword : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_sec_010_openLockedPdfPrompts();
    void uat_sec_011_wrongPasswordKeepsLocked();
    void uat_sec_012_cancelPasswordAborts();
    void uat_sec_020_exportWithPasswordRoundTrip();

  private:
    QTemporaryDir m_scratch;
};

void TestUatPassword::init() {
    PdfAdapter::setPasswordPrompt({}); // restore default each time
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-SEC-010 — Opening a locked PDF prompts for a password and
// succeeds when the right one is supplied. The document becomes fully
// valid: it has a page count > 0 and the window title reflects the
// filename (not an error state).
void TestUatPassword::uat_sec_010_openLockedPdfPrompts() {
    QVERIFY(m_scratch.isValid());
    const QString pw = QStringLiteral("correcthorsebatterystaple");
    const QString locked =
        writeLockedPdf(m_scratch.filePath(QStringLiteral("sec010_locked.pdf")), pw);

    // Install the prompt shim before calling openFiles so it intercepts
    // the PdfAdapter::open password loop.
    PromptGuard guard({pw});

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({locked});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY2(doc, "Document should be non-null after successful unlock");
    QVERIFY2(doc->pageCount() > 0, "Unlocked PDF should report at least one page");
    QVERIFY2(!doc->displayName().isEmpty(), "Display name should be set");
}

// UAT-SEC-011 — Three wrong passwords exhaust the prompt budget.
// The document stays in the locked state (pageCount == 0). The app
// must not crash or assert.
void TestUatPassword::uat_sec_011_wrongPasswordKeepsLocked() {
    QVERIFY(m_scratch.isValid());
    const QString locked = writeLockedPdf(m_scratch.filePath(QStringLiteral("sec011_locked.pdf")),
                                          QStringLiteral("the-real-password"));

    // Supply three wrong passwords — after these the adapter gives up.
    PromptGuard guard(
        {QStringLiteral("wrong1"), QStringLiteral("wrong2"), QStringLiteral("wrong3")});

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({locked});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 0); // still locked → no pages visible
}

// UAT-SEC-012 — The user pressing Cancel (nullopt) immediately stops
// prompting. Same end state as SEC-011 but via a single cancel rather
// than exhausting the budget.
void TestUatPassword::uat_sec_012_cancelPasswordAborts() {
    QVERIFY(m_scratch.isValid());
    const QString locked = writeLockedPdf(m_scratch.filePath(QStringLiteral("sec012_locked.pdf")),
                                          QStringLiteral("secret"));

    // Empty queue → first prompt returns nullopt → cancel path.
    PromptGuard guard({});

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({locked});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    IDocument *doc = dv ? dv->currentDocument() : nullptr;
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 0);
}

// UAT-SEC-020 — Export as Password-Protected PDF writes an encrypted
// file: the exported PDF (a) cannot be opened without the right
// password, and (b) can be opened with it and preserves the page
// count.
void TestUatPassword::uat_sec_020_exportWithPasswordRoundTrip() {
    QVERIFY(m_scratch.isValid());
    const QString srcPath = writeSample(m_scratch.filePath(QStringLiteral("sec020_src.pdf")));
    const QString dstPath = m_scratch.filePath(QStringLiteral("sec020_encrypted.pdf"));
    const QString pw = QStringLiteral("phase5-export-pw");

    // Open the plain PDF via the application layer.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({srcPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QVERIFY(doc->supportsPasswordExport());
    const int srcPages = doc->pageCount();
    QVERIFY(srcPages > 0);

    // Invoke the export directly on the document (bypasses the file
    // and password dialogs that only work interactively).
    QVERIFY(doc->exportWithPassword(dstPath, pw));
    QVERIFY(QFileInfo::exists(dstPath));

    // Verify the output is actually encrypted: opening without a
    // password must fail and report needsPassword.
    PdfEditor probe;
    QVERIFY(!probe.load(dstPath));
    QVERIFY2(probe.isEncrypted(), "Exported PDF should refuse to open without a password");

    // Verify the output opens with the right password.
    QVERIFY(probe.unlock(pw));
    QCOMPARE(probe.pageCount(), srcPages);
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

    Application app(argc, argv);
    TestUatPassword tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_password.moc"
