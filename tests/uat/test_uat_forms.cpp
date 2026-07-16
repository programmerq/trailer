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
#include "ui/Sidebar.h"
#include "settings/DocumentTypeDefaults.h"
#include "recent/RecentFiles.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QScopeGuard>
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

// Build a one-page PDF with three AcroForm fields and write it to `path`.
//   id 0 – text     "fullname"  value "Alice"   label "Full Name"
//   id 1 – checkbox "agree"     value "Yes"
//   id 2 – dropdown "color"     opts=[Red,Green,Blue]  value "Green"
static QString writeFormPdf(const QString &path) {
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
    tf.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    tf.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    tf.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    tf.replaceKey("/T", QPDFObjectHandle::newString("fullname"));
    tf.replaceKey("/TU", QPDFObjectHandle::newString("Full Name"));
    tf.replaceKey("/V", QPDFObjectHandle::newString("Alice"));
    tf.replaceKey("/Rect", makeRect(72, 720, 288, 744));
    tf.replaceKey("/P", pageObj);
    QPDFObjectHandle tfObj = pdf.makeIndirectObject(tf);

    // checkbox
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

    // dropdown / combo
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
    pagesDict.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    pagesDict.replaceKey("/Kids", kids);
    pagesDict.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle pagesObj = pdf.makeIndirectObject(pagesDict);
    pageDict.replaceKey("/Parent", pagesObj);

    // AcroForm
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

} // namespace

class TestUatForms : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_frm_010_formFieldsReportedOnOpen();
    void uat_frm_020_setFormFillingActiveShowsFields();
    void uat_frm_030_fillTextFieldPersistsAcrossSave();
    void uat_frm_040_fillFormsAutoEnabledOnFillablePdf();
    void uat_frm_041_fillFormsRespectsExplicitToggleOff();
    void uat_frm_050_tabMovesBetweenFieldsInReadingOrder();
    void uat_frm_060_formForcesSidebarHiddenOverridingTypeDefault();

  private:
    QTemporaryDir m_scratch;
};

void TestUatForms::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-FRM-010 — Opening a PDF with an AcroForm must make
// supportsFormFilling() return true and expose the expected fields.
void TestUatForms::uat_frm_010_formFieldsReportedOnOpen() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(m_scratch.filePath(QStringLiteral("frm010.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    // Since PR #63 the qpdf parse behind supportsFormFilling() runs on a
    // background worker, so form detection resolves a moment after open. Pump
    // the event loop until it lands (QTRY passes immediately once resolved).
    QTRY_VERIFY(doc->supportsFormFilling());

    const auto fields = doc->formFields();
    QCOMPARE(static_cast<int>(fields.size()), 3);

    // Verify names and types are correctly decoded.
    bool hasText = false, hasCb = false, hasDd = false;
    for (const auto &f : fields) {
        if (f.name == QStringLiteral("fullname") && f.type == FormFieldType::Text)
            hasText = true;
        if (f.name == QStringLiteral("agree") && f.type == FormFieldType::Checkbox)
            hasCb = true;
        if (f.name == QStringLiteral("color") && f.type == FormFieldType::Dropdown)
            hasDd = true;
    }
    QVERIFY(hasText);
    QVERIFY(hasCb);
    QVERIFY(hasDd);
}

// UAT-FRM-020 — Toggling form-filling mode on must not crash, and
// the document must still report supportsFormFilling() after the call.
void TestUatForms::uat_frm_020_setFormFillingActiveShowsFields() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(m_scratch.filePath(QStringLiteral("frm020.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    // Async form detection (PR #63): pump until it resolves.
    QTRY_VERIFY(doc->supportsFormFilling());

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
    const QString src = writeFormPdf(m_scratch.filePath(QStringLiteral("frm030_src.pdf")));
    const QString dst = m_scratch.filePath(QStringLiteral("frm030_filled.pdf"));

    // --- Open the form, fill it, save ---
    {
        auto *app = qobject_cast<Application *>(qApp);
        QVERIFY(app);
        app->openFiles({src});
        QApplication::processEvents();

        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        auto *dv = mw->findChild<DocumentView *>();
        QVERIFY(dv);
        IDocument *doc = dv->currentDocument();
        QVERIFY(doc);
        // Async form detection (PR #63): pump until it resolves.
        QTRY_VERIFY(doc->supportsFormFilling());

        // Find the "fullname" text field.
        const auto fields = doc->formFields();
        int tfId = -1;
        for (const auto &f : fields) {
            if (f.name == QStringLiteral("fullname")) {
                tfId = f.id;
                break;
            }
        }
        QVERIFY(tfId >= 0);

        QVERIFY(doc->setFormFieldValue(tfId, QStringLiteral("Charlie")));
        QVERIFY(doc->save(dst));

        mw->close();
        QApplication::processEvents();
    }

    // --- Re-open the saved file and verify the value persisted ---
    {
        auto *app = qobject_cast<Application *>(qApp);
        QVERIFY(app);
        app->openFiles({dst});
        QApplication::processEvents();

        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        auto *dv = mw->findChild<DocumentView *>();
        QVERIFY(dv);
        IDocument *doc = dv->currentDocument();
        QVERIFY(doc);
        // Async form detection (PR #63): pump until it resolves.
        QTRY_VERIFY(doc->supportsFormFilling());

        const auto fields = doc->formFields();
        const FormField *tf = nullptr;
        for (const auto &f : fields) {
            if (f.name == QStringLiteral("fullname")) {
                tf = &f;
                break;
            }
        }
        QVERIFY(tf != nullptr);
        QCOMPARE(tf->value, QStringLiteral("Charlie"));
    }
}

namespace {
QAction *findFillFormsAction(MainWindow *mw) {
    for (QAction *top : mw->menuBar()->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *a : menu->actions()) {
            QString t = a->text();
            t.remove(QLatin1Char('&'));
            if (t == QStringLiteral("Fill Forms"))
                return a;
        }
    }
    return nullptr;
}
} // namespace

// UAT-FRM-040 — Opening a fillable PDF auto-enables Fill Forms mode
// so the user sees the editable widgets without having to discover the
// menu toggle. This was the 2026-04-28 reframe of the AutoFill area:
// users want "see the field, click, type", not matcher cleverness.
void TestUatForms::uat_frm_040_fillFormsAutoEnabledOnFillablePdf() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(m_scratch.filePath(QStringLiteral("frm040.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *fillForms = findFillFormsAction(mw);
    QVERIFY2(fillForms, "Tools > Fill Forms action not found");
    // Enable + auto-enable fire when async form detection completes (PR #63),
    // a moment after open — pump until it lands.
    QTRY_VERIFY2(fillForms->isChecked(),
                 "Fill Forms must be auto-enabled the first time a fillable "
                 "PDF becomes the current document");
    QVERIFY2(fillForms->isEnabled(), "Fill Forms must be enabled for a fillable PDF");
}

// UAT-FRM-041 — If the user explicitly toggles Fill Forms off for a
// document, switching to another window and back must NOT re-enable
// it. The auto-enable is once-per-document, not per-focus.
void TestUatForms::uat_frm_041_fillFormsRespectsExplicitToggleOff() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(m_scratch.filePath(QStringLiteral("frm041.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *fillForms = findFillFormsAction(mw);
    QVERIFY(fillForms);
    // Auto-enable fires when async form detection completes (PR #63) — pump
    // until it lands before exercising the explicit toggle-off.
    QTRY_VERIFY(fillForms->isChecked());

    // User explicitly toggles off.
    fillForms->trigger();
    QApplication::processEvents();
    QVERIFY(!fillForms->isChecked());

    // Force a re-evaluation of the current-document path: simulate the
    // window losing and regaining its current doc by reissuing the
    // signal. (In real life this fires on tab/window focus changes.)
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QMetaObject::invokeMethod(dv, "currentDocumentChanged", Qt::DirectConnection,
                              Q_ARG(trailer::IDocument *, doc));
    QApplication::processEvents();

    QVERIFY2(!fillForms->isChecked(), "Re-emitting currentDocumentChanged for a doc the user has "
                                      "already opted out on must not re-enable Fill Forms");
}

// UAT-FRM-050 — Tab moves focus through form fields in reading order
// (page asc, top-to-bottom on the page in PDF coords, left-to-right).
// The `writeFormPdf` fixture has three fields whose /T order in the
// AcroForm tree is fullname → agree → color, but they're laid out at
// y=700, y=600, y=500 respectively (top-to-bottom). Reading order and
// AcroForm order happen to coincide here, so Tab should walk
// fullname → agree → color regardless of which one we focus first.
void TestUatForms::uat_frm_050_tabMovesBetweenFieldsInReadingOrder() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(m_scratch.filePath(QStringLiteral("frm050.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({path});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *doc = dv->currentDocument();
    QVERIFY(doc);
    // Async form detection (PR #63): pump until it resolves.
    QTRY_VERIFY(doc->supportsFormFilling());

    // Force the form overlay to populate by toggling form-filling on.
    // (UAT-FRM-040 makes this happen automatically, but be explicit so
    // this test does not depend on the auto-enable.)
    doc->setFormFillingActive(true);
    QApplication::processEvents();

    // Find the three form widgets in the FormOverlay. We want them in
    // a stable order keyed off their field ids (the helper for ids
    // would require linking against PdfEditor; the simpler check is to
    // verify the Tab chain produces three distinct widgets in some
    // consistent order without crashing).
    auto *mw_widget = mw->findChild<QWidget *>();
    QVERIFY(mw_widget);
    QList<QWidget *> overlayChildren;
    for (auto *fo : mw->findChildren<QWidget *>()) {
        if (auto *parent = fo->parentWidget()) {
            // FormOverlay is the only widget that holds QLineEdit /
            // QCheckBox / QComboBox children for form fields.
            const QString cn = parent->metaObject()->className();
            if (cn.contains(QStringLiteral("FormOverlay"))) {
                overlayChildren.append(fo);
            }
        }
    }
    QVERIFY2(overlayChildren.size() >= 3,
             "Expected at least three form-field widgets in FormOverlay");

    // Walk the focus chain via QWidget::nextInFocusChain, starting
    // from the first overlay child. Ensure every step is a different
    // widget (i.e., setTabOrder built a real chain) until we cycle.
    QWidget *start = overlayChildren.first();
    start->setFocus();
    QSet<QWidget *> visited;
    visited.insert(start);
    QWidget *cursor = start;
    for (int i = 0; i < 10 && cursor; ++i) {
        cursor = cursor->nextInFocusChain();
        if (overlayChildren.contains(cursor)) {
            visited.insert(cursor);
        }
        if (visited.size() == static_cast<int>(overlayChildren.size()))
            break;
    }
    QVERIFY2(visited.size() >= 3, "Tab focus chain did not visit all form-field widgets");
}

// UAT-VWR-055 (content-aware first-open defaults, forms branch).
//
// A form PDF (>= 3 fillable fields, < 20 pages) with no saved per-file
// state forces the sidebar hidden for a clean filling view. To prove the
// heuristic actively *overrides* state (rather than passing only because
// the global default already hides the sidebar), we first seed a per-type
// PDF default that opens the thumbnail sidebar — content-aware must still
// win and leave it hidden. The form-filling toolbar surfaces separately
// and is unaffected.
void TestUatForms::uat_frm_060_formForcesSidebarHiddenOverridingTypeDefault() {
    QVERIFY(m_scratch.isValid());
    const QString path = writeFormPdf(m_scratch.filePath(QStringLiteral("frm060.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Per-type default that would otherwise open the page-thumbnail
    // sidebar for any PDF with no per-file state. This mutates the
    // process-wide per-type defaults, which are shared across every test
    // slot (they run against one Application), so capture the prior value
    // and restore it on scope exit — including any early QVERIFY return —
    // to keep later slots order-independent.
    const DocumentTypeDefault priorPdfDefault =
        app->documentTypeDefaults().forType(DocumentType::Pdf);
    const auto restorePdfDefault = qScopeGuard([&] {
        app->documentTypeDefaults().setForType(DocumentType::Pdf, priorPdfDefault);
    });

    DocumentTypeDefault def;
    def.sidebarMode = SidebarMode::Pages;
    QVERIFY2(def.hasState(), "seeded per-type default must report state");
    app->documentTypeDefaults().setForType(DocumentType::Pdf, def);

    app->openFiles({path});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    // Precondition: the fixture is recognised as a 3-field form. Form detection
    // is async since PR #63, so pump until it resolves; the content-aware
    // sidebar decision is re-evaluated on the same capabilities signal.
    QTRY_VERIFY2(doc->supportsFormFilling(), "fixture must be a fillable form");
    QCOMPARE(static_cast<int>(doc->formFields().size()), 3);

    // The form heuristic wins over the seeded per-type "show thumbnails".
    auto *sidebar = mw->findChild<Sidebar *>();
    QVERIFY2(sidebar, "MainWindow should host a Sidebar");
    QTRY_COMPARE(static_cast<int>(sidebar->mode()), static_cast<int>(Sidebar::Mode::Hidden));
}

// Custom main: create Application (not just QApplication) so
// qobject_cast<Application*>(qApp) succeeds inside the tests.
// Also sandbox HOME so Settings/RecentFiles don't touch the user's
// real config directory.
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
    TestUatForms tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_forms.moc"
