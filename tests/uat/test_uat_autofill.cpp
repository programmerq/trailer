// UAT harness — AutoFill cards (Phase 5).
//
// Seeds a CardStore with a known MyCard, opens a form PDF, runs
// autoFillDocument(), and verifies that the matching fields were
// populated from the card — effectively covering the 80%-of-a-W9
// acceptance criterion with a miniature form.
//
//   uat_af_010_autoFillPopulatesMatchingTextFields
//       Running autoFillDocument against a seeded card and a form
//       PDF fills every text field whose name matches a card
//       attribute, leaving unrecognised fields alone.
//   uat_af_020_autoFillFallsBackToAlternativeName
//       A PDF with opaque /T names ("f1_03") but friendly /TU labels
//       ("First Name") is matched via the /TU fallback.
//   uat_af_030_autoFillMenuActionShowsStatusAndActivatesForm
//       The Tools > AutoFill Form menu action used to pop a modal
//       QMessageBox. After the 2026-04-24 HITL fix it writes the
//       result to the status bar and turns on form-filling so the
//       filled values are visible without the user having to toggle
//       a separate mode.

#include "app/Application.h"
#include "cards/CardStore.h"
#include "cards/MyCard.h"
#include "document/IDocument.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "settings/AppPaths.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

MainWindow* currentMainWindow() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) return mw;
    }
    return nullptr;
}

// Walk every menu's actions and return the action whose visible text
// matches, ignoring "&" accelerator hints. Case-sensitive.
QAction* findMenuAction(MainWindow* mw, const QString& text) {
    for (QAction* a : mw->menuBar()->actions()) {
        QMenu* m = a->menu();
        if (!m) continue;
        for (QAction* sub : m->actions()) {
            QString t = sub->text();
            t.remove(QLatin1Char('&'));
            if (t == text) return sub;
        }
    }
    return nullptr;
}

// Minimal two-field form PDF: one text field ("email") and one that
// won't match any card attribute ("signature_placeholder"). Keeps the
// test assertion crisp: fill one, leave one alone.
QString writeMiniFormPdf(const QString& path) {
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

    // Page
    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    page.replaceKey("/MediaBox", makeRect(0, 0, 612, 792));
    page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());

    // Field 0 — text "email"
    QPDFObjectHandle emailField = QPDFObjectHandle::newDictionary();
    emailField.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    emailField.replaceKey("/T", QPDFObjectHandle::newString("email"));
    emailField.replaceKey("/V", QPDFObjectHandle::newString(""));
    emailField.replaceKey("/Rect", makeRect(50, 700, 300, 720));
    emailField.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    emailField = pdf.makeIndirectObject(emailField);

    // Field 1 — text "signature_placeholder" (unknown to the matcher)
    QPDFObjectHandle sigField = QPDFObjectHandle::newDictionary();
    sigField.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    sigField.replaceKey("/T", QPDFObjectHandle::newString("signature_placeholder"));
    sigField.replaceKey("/V", QPDFObjectHandle::newString(""));
    sigField.replaceKey("/Rect", makeRect(50, 650, 300, 670));
    sigField.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    sigField = pdf.makeIndirectObject(sigField);

    // Wire the widgets to the page.
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(emailField);
    annots.appendItem(sigField);
    page.replaceKey("/Annots", annots);

    QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);
    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    QPDFObjectHandle pagesRef = pdf.makeIndirectObject(pages);
    pageRef.replaceKey("/Parent", pagesRef);

    QPDFObjectHandle acro = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle fields = QPDFObjectHandle::newArray();
    fields.appendItem(emailField);
    fields.appendItem(sigField);
    acro.replaceKey("/Fields", fields);

    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages", pagesRef);
    root.replaceKey("/AcroForm", acro);

    QPDFWriter writer(pdf, path.toLocal8Bit().constData());
    writer.setStaticID(true);
    writer.write();
    return path;
}

// Variant that assigns cryptic /T names and puts the friendly label
// on /TU (AcroForm's "alternative name" — what most viewers show next
// to the field). Used to verify AutoFill's /TU fallback.
QString writeFormPdfWithTooltipLabels(const QString& path) {
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

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    page.replaceKey("/MediaBox", makeRect(0, 0, 612, 792));
    page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());

    auto makeTextField = [&pdf, &makeRect](const std::string& t,
                                           const std::string& tu,
                                           double y) {
        QPDFObjectHandle f = QPDFObjectHandle::newDictionary();
        f.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        f.replaceKey("/T", QPDFObjectHandle::newString(t));
        f.replaceKey("/TU", QPDFObjectHandle::newString(tu));
        f.replaceKey("/V", QPDFObjectHandle::newString(""));
        f.replaceKey("/Rect", makeRect(50, y, 300, y + 20));
        f.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        return pdf.makeIndirectObject(f);
    };

    // Cryptic /T, friendly /TU.
    auto firstField = makeTextField("f1_03", "First Name", 700);
    auto emailField = makeTextField("f2_01", "Email Address", 660);
    auto mysteryField = makeTextField("f9_99", "Supplementary Code", 620);

    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(firstField);
    annots.appendItem(emailField);
    annots.appendItem(mysteryField);
    page.replaceKey("/Annots", annots);

    QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);
    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    QPDFObjectHandle pagesRef = pdf.makeIndirectObject(pages);
    pageRef.replaceKey("/Parent", pagesRef);

    QPDFObjectHandle acro = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle fields = QPDFObjectHandle::newArray();
    fields.appendItem(firstField);
    fields.appendItem(emailField);
    fields.appendItem(mysteryField);
    acro.replaceKey("/Fields", fields);

    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages", pagesRef);
    root.replaceKey("/AcroForm", acro);

    QPDFWriter writer(pdf, path.toLocal8Bit().constData());
    writer.setStaticID(true);
    writer.write();
    return path;
}

}  // namespace

class TestUatAutoFill : public QObject {
    Q_OBJECT

private slots:
    void init();
    void uat_af_010_autoFillPopulatesMatchingTextFields();
    void uat_af_020_autoFillFallsBackToAlternativeName();
    void uat_af_030_autoFillMenuActionShowsStatusAndActivatesForm();

private:
    QTemporaryDir m_scratch;
};

void TestUatAutoFill::init() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) w->close();
    }
    QApplication::processEvents();
}

void TestUatAutoFill::uat_af_010_autoFillPopulatesMatchingTextFields() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeMiniFormPdf(
        m_scratch.filePath(QStringLiteral("autofill010.pdf")));

    // Seed the card store on disk so MainWindow's AutoFill handler
    // picks it up without prompting.
    {
        CardStore store(AppPaths::cardsFile());
        MyCard c;
        c.label = QStringLiteral("Test Card");
        c.givenName = QStringLiteral("Alice");
        c.familyName = QStringLiteral("Example");
        c.email = QStringLiteral("alice@example.com");
        store.addCard(std::move(c));
        store.save();
    }

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);
    QVERIFY(doc->supportsFormFilling());

    // Also verify the Tools > AutoFill Form action is present and
    // enabled — but don't trigger it, because it pops a modal
    // QMessageBox that blocks the offscreen test.
    QAction* action = findMenuAction(mw, QStringLiteral("AutoFill Form"));
    QVERIFY(action);
    QVERIFY(action->isEnabled());

    // Drive the helper directly, as if the dialog had been dismissed.
    CardStore store(AppPaths::cardsFile());
    store.load();
    QVERIFY(store.hasActive());
    const AutoFillResult r = autoFillDocument(doc, store.activeCard());
    QCOMPARE(r.examined, 2);
    QCOMPARE(r.filled, 1);

    // Now inspect the fields. "email" should have been filled, the
    // unknown field should still be empty.
    const auto fields = doc->formFields();
    const FormField* emailField = nullptr;
    const FormField* sigField = nullptr;
    for (const auto& f : fields) {
        if (f.name == QStringLiteral("email")) emailField = &f;
        if (f.name == QStringLiteral("signature_placeholder")) sigField = &f;
    }
    QVERIFY(emailField != nullptr);
    QVERIFY(sigField != nullptr);
    QCOMPARE(emailField->value, QStringLiteral("alice@example.com"));
    QCOMPARE(sigField->value, QString());
}

void TestUatAutoFill::uat_af_020_autoFillFallsBackToAlternativeName() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdfWithTooltipLabels(
        m_scratch.filePath(QStringLiteral("autofill020.pdf")));

    {
        CardStore store(AppPaths::cardsFile());
        MyCard c;
        c.label = QStringLiteral("TU Fallback");
        c.givenName = QStringLiteral("Alice");
        c.email = QStringLiteral("alice@example.com");
        store.addCard(std::move(c));
        store.save();
    }

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);

    CardStore store(AppPaths::cardsFile());
    store.load();
    QVERIFY(store.hasActive());
    const AutoFillResult r = autoFillDocument(doc, store.activeCard());

    QCOMPARE(r.examined, 3);
    // First Name via /TU, Email Address via /TU — "Supplementary Code"
    // still doesn't match any card attribute.
    QCOMPARE(r.filled, 2);

    const auto fields = doc->formFields();
    const FormField* first = nullptr;
    const FormField* email = nullptr;
    const FormField* mystery = nullptr;
    for (const auto& f : fields) {
        if (f.label == QStringLiteral("First Name")) first = &f;
        if (f.label == QStringLiteral("Email Address")) email = &f;
        if (f.label == QStringLiteral("Supplementary Code")) mystery = &f;
    }
    QVERIFY(first);
    QVERIFY(email);
    QVERIFY(mystery);
    QCOMPARE(first->value, QStringLiteral("Alice"));
    QCOMPARE(email->value, QStringLiteral("alice@example.com"));
    QCOMPARE(mystery->value, QString());
}

void TestUatAutoFill::
    uat_af_030_autoFillMenuActionShowsStatusAndActivatesForm() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeMiniFormPdf(
        m_scratch.filePath(QStringLiteral("autofill030.pdf")));

    {
        CardStore store(AppPaths::cardsFile());
        MyCard c;
        c.label = QStringLiteral("Status Line");
        c.email = QStringLiteral("alice@example.com");
        store.addCard(std::move(c));
        store.save();
    }

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);

    // Fill Forms starts off; AutoFill should turn it on once it
    // writes at least one value.
    QAction* fillForms = findMenuAction(mw, QStringLiteral("Fill Forms"));
    QVERIFY(fillForms);
    QVERIFY(!fillForms->isChecked());

    QAction* autoFill = findMenuAction(mw, QStringLiteral("AutoFill Form"));
    QVERIFY(autoFill);
    QVERIFY(autoFill->isEnabled());

    // The pre-fix implementation popped QMessageBox::information here
    // and would hang the offscreen test. Post-fix it writes to the
    // status bar — the call returns immediately.
    autoFill->trigger();
    QApplication::processEvents();

    // Status bar carries the success line (at least for the 8-second
    // timeout). "Filled 1 of 2" is what the mini-form yields.
    const QString msg = mw->statusBar()->currentMessage();
    QVERIFY2(msg.contains(QStringLiteral("AutoFill")),
             qPrintable(QStringLiteral("status bar was: '%1'").arg(msg)));
    QVERIFY2(msg.contains(QStringLiteral("1")) &&
             msg.contains(QStringLiteral("2")),
             qPrintable(QStringLiteral("expected 'filled 1 of 2' in: '%1'")
                            .arg(msg)));

    // Form-filling mode should now be active so the user sees the
    // newly-filled value in the overlay.
    QVERIFY2(fillForms->isChecked(),
             "Fill Forms action should be checked after a non-empty AutoFill");
}

// Custom main: create Application (not just QApplication) so
// qobject_cast<Application*>(qApp) succeeds. Sandbox HOME so the
// CardStore and RecentFiles don't touch the developer's real config.
int main(int argc, char** argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid()) return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME",   (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatAutoFill tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_autofill.moc"
