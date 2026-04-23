// UAT harness — AcroForm field filling (Phase 5)
//
// Covers reading form fields from a PDF, toggling form-filling mode,
// updating field values through the document interface, and persisting
// them across a save/reload cycle.
//
//   uat_frm_010_formFieldsReportedOnOpen
//       Opening a PDF with AcroForm fields reports supportsFormFilling()
//       = true and returns the expected field list.
//
//   uat_frm_020_setFormFillingActiveShowsFields
//       Calling setFormFillingActive(true) on a form PDF does not crash
//       and returns a document that still supports the capability.
//
//   uat_frm_030_fillTextFieldPersistsAcrossSave
//       Setting a text-field value and saving produces a file that,
//       when re-opened, reports the new value for that field.

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDir>
#include <QFileInfo>
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

// Build a one-page PDF with three AcroForm fields and write it to `path`.
//   id 0 – text     "fullname"  value "Alice"   label "Full Name"
//   id 1 – checkbox "agree"     value "Yes"
//   id 2 – dropdown "color"     opts=[Red,Green,Blue]  value "Green"
static QString writeFormPdf(const QString& path) {
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

    // page
    QPDFObjectHandle pageDict = QPDFObjectHandle::newDictionary();
    pageDict.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle mb = QPDFObjectHandle::newArray();
    for (int v : {0, 0, 612, 792})
        mb.appendItem(QPDFObjectHandle::newInteger(v));
    pageDict.replaceKey("/MediaBox", mb);
    QPDFObjectHandle pageObj = pdf.makeIndirectObject(pageDict);

    // text field
    QPDFObjectHandle tf = QPDFObjectHandle::newDictionary();
    tf.replaceKey("/Type",    QPDFObjectHandle::newName("/Annot"));
    tf.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    tf.replaceKey("/FT",      QPDFObjectHandle::newName("/Tx"));
    tf.replaceKey("/T",       QPDFObjectHandle::newString("fullname"));
    tf.replaceKey("/TU",      QPDFObjectHandle::newString("Full Name"));
    tf.replaceKey("/V",       QPDFObjectHandle::newString("Alice"));
    tf.replaceKey("/Rect",    makeRect(72, 720, 288, 744));
    tf.replaceKey("/P",       pageObj);
    QPDFObjectHandle tfObj = pdf.makeIndirectObject(tf);

    // checkbox
    QPDFObjectHandle cb = QPDFObjectHandle::newDictionary();
    cb.replaceKey("/Type",    QPDFObjectHandle::newName("/Annot"));
    cb.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    cb.replaceKey("/FT",      QPDFObjectHandle::newName("/Btn"));
    cb.replaceKey("/T",       QPDFObjectHandle::newString("agree"));
    cb.replaceKey("/V",       QPDFObjectHandle::newName("/Yes"));
    cb.replaceKey("/AS",      QPDFObjectHandle::newName("/Yes"));
    cb.replaceKey("/Rect",    makeRect(72, 680, 96, 704));
    cb.replaceKey("/P",       pageObj);
    QPDFObjectHandle cbObj = pdf.makeIndirectObject(cb);

    // dropdown / combo
    QPDFObjectHandle dd = QPDFObjectHandle::newDictionary();
    dd.replaceKey("/Type",    QPDFObjectHandle::newName("/Annot"));
    dd.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    dd.replaceKey("/FT",      QPDFObjectHandle::newName("/Ch"));
    dd.replaceKey("/Ff",      QPDFObjectHandle::newInteger(131072));
    dd.replaceKey("/T",       QPDFObjectHandle::newString("color"));
    dd.replaceKey("/V",       QPDFObjectHandle::newString("Green"));
    QPDFObjectHandle opts = QPDFObjectHandle::newArray();
    for (const char* s : {"Red", "Green", "Blue"})
        opts.appendItem(QPDFObjectHandle::newString(s));
    dd.replaceKey("/Opt",  opts);
    dd.replaceKey("/Rect", makeRect(72, 640, 288, 664));
    dd.replaceKey("/P",    pageObj);
    QPDFObjectHandle ddObj = pdf.makeIndirectObject(dd);

    // wire /Annots
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(tfObj);
    annots.appendItem(cbObj);
    annots.appendItem(ddObj);
    pageDict.replaceKey("/Annots", annots);

    // /Pages
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageObj);
    QPDFObjectHandle pagesDict = QPDFObjectHandle::newDictionary();
    pagesDict.replaceKey("/Type",  QPDFObjectHandle::newName("/Pages"));
    pagesDict.replaceKey("/Kids",  kids);
    pagesDict.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle pagesObj = pdf.makeIndirectObject(pagesDict);
    pageDict.replaceKey("/Parent", pagesObj);

    // AcroForm
    QPDFObjectHandle acroFields = QPDFObjectHandle::newArray();
    acroFields.appendItem(tfObj);
    acroFields.appendItem(cbObj);
    acroFields.appendItem(ddObj);
    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    acroForm.replaceKey("/Fields",          acroFields);
    acroForm.replaceKey("/NeedAppearances", QPDFObjectHandle::newBool(true));

    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages",    pagesObj);
    root.replaceKey("/AcroForm", acroForm);

    QPDFWriter writer(pdf, path.toStdString().c_str());
    writer.write();
    return path;
}

}  // namespace

class TestUatForms : public QObject {
    Q_OBJECT
private slots:
    void init();

    void uat_frm_010_formFieldsReportedOnOpen();
    void uat_frm_020_setFormFillingActiveShowsFields();
    void uat_frm_030_fillTextFieldPersistsAcrossSave();

private:
    QTemporaryDir m_scratch;
};

void TestUatForms::init() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) w->close();
    }
    QApplication::processEvents();
}

// UAT-FRM-010 — Opening a PDF with an AcroForm must make
// supportsFormFilling() return true and expose the expected fields.
void TestUatForms::uat_frm_010_formFieldsReportedOnOpen() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(
        m_scratch.filePath(QStringLiteral("frm010.pdf")));

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

    const auto fields = doc->formFields();
    QCOMPARE(static_cast<int>(fields.size()), 3);

    // Verify names and types are correctly decoded.
    bool hasText = false, hasCb = false, hasDd = false;
    for (const auto& f : fields) {
        if (f.name == QStringLiteral("fullname") &&
            f.type == FormFieldType::Text) hasText = true;
        if (f.name == QStringLiteral("agree") &&
            f.type == FormFieldType::Checkbox) hasCb = true;
        if (f.name == QStringLiteral("color") &&
            f.type == FormFieldType::Dropdown) hasDd = true;
    }
    QVERIFY(hasText);
    QVERIFY(hasCb);
    QVERIFY(hasDd);
}

// UAT-FRM-020 — Toggling form-filling mode on must not crash, and
// the document must still report supportsFormFilling() after the call.
void TestUatForms::uat_frm_020_setFormFillingActiveShowsFields() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(
        m_scratch.filePath(QStringLiteral("frm020.pdf")));

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

    // Toggle on then off — must not crash on either transition.
    doc->setFormFillingActive(true);
    QApplication::processEvents();
    doc->setFormFillingActive(false);
    QApplication::processEvents();

    QVERIFY(doc->supportsFormFilling());
}

// UAT-FRM-030 — Setting a field value and saving must persist the new
// value across a full close/reopen cycle.
void TestUatForms::uat_frm_030_fillTextFieldPersistsAcrossSave() {
    QVERIFY(m_scratch.isValid());
    const QString src = writeFormPdf(
        m_scratch.filePath(QStringLiteral("frm030_src.pdf")));
    const QString dst = m_scratch.filePath(
        QStringLiteral("frm030_filled.pdf"));

    // --- Open the form, fill it, save ---
    {
        auto* app = qobject_cast<Application*>(qApp);
        QVERIFY(app);
        app->openFiles({src});
        QApplication::processEvents();

        MainWindow* mw = currentMainWindow();
        QVERIFY(mw);
        auto* dv = mw->findChild<DocumentView*>();
        QVERIFY(dv);
        IDocument* doc = dv->currentDocument();
        QVERIFY(doc);
        QVERIFY(doc->supportsFormFilling());

        // Find the "fullname" text field.
        const auto fields = doc->formFields();
        int tfId = -1;
        for (const auto& f : fields) {
            if (f.name == QStringLiteral("fullname")) { tfId = f.id; break; }
        }
        QVERIFY(tfId >= 0);

        QVERIFY(doc->setFormFieldValue(tfId, QStringLiteral("Charlie")));
        QVERIFY(doc->save(dst));

        mw->close();
        QApplication::processEvents();
    }

    // --- Re-open the saved file and verify the value persisted ---
    {
        auto* app = qobject_cast<Application*>(qApp);
        QVERIFY(app);
        app->openFiles({dst});
        QApplication::processEvents();

        MainWindow* mw = currentMainWindow();
        QVERIFY(mw);
        auto* dv = mw->findChild<DocumentView*>();
        QVERIFY(dv);
        IDocument* doc = dv->currentDocument();
        QVERIFY(doc);
        QVERIFY(doc->supportsFormFilling());

        const auto fields = doc->formFields();
        const FormField* tf = nullptr;
        for (const auto& f : fields) {
            if (f.name == QStringLiteral("fullname")) { tf = &f; break; }
        }
        QVERIFY(tf != nullptr);
        QCOMPARE(tf->value, QStringLiteral("Charlie"));
    }
}

// Custom main: create Application (not just QApplication) so
// qobject_cast<Application*>(qApp) succeeds inside the tests.
// Also sandbox HOME so Settings/RecentFiles don't touch the user's
// real config directory.
int main(int argc, char** argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid()) return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME",   (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatForms tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_forms.moc"
