#include "document/PdfEditor.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

QString writeSamplePdf(const QString& path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    for (int i = 0; i < pages; ++i) {
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                         QStringLiteral("Page %1").arg(i + 1));
        if (i < pages - 1) {
            writer.newPage();
        }
    }
    painter.end();
    return path;
}

// Build a one-page PDF that contains three AcroForm fields:
//   id 0 – text      "fullname"  value "Alice"   label "Full Name"
//   id 1 – checkbox  "agree"     value "Yes"
//   id 2 – dropdown  "color"     opts [Red,Green,Blue]  value "Green"
//
// Uses the qpdf C++ API directly so we can produce form fields that
// Qt's QPdfWriter doesn't expose.
static QString writeFormPdf(const QString& path) {
    QPDF pdf;
    pdf.emptyPDF();

    // Helper: [x0 y0 x1 y1] rectangle array (PDF bottom-left origin)
    auto makeRect = [](double x0, double y0, double x1, double y1) {
        QPDFObjectHandle r = QPDFObjectHandle::newArray();
        r.appendItem(QPDFObjectHandle::newReal(x0));
        r.appendItem(QPDFObjectHandle::newReal(y0));
        r.appendItem(QPDFObjectHandle::newReal(x1));
        r.appendItem(QPDFObjectHandle::newReal(y1));
        return r;
    };

    // ---- page ----
    QPDFObjectHandle pageDict = QPDFObjectHandle::newDictionary();
    pageDict.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle mb = QPDFObjectHandle::newArray();
    for (int v : {0, 0, 612, 792})
        mb.appendItem(QPDFObjectHandle::newInteger(v));
    pageDict.replaceKey("/MediaBox", mb);
    QPDFObjectHandle pageObj = pdf.makeIndirectObject(pageDict);

    // ---- text field ----
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

    // ---- checkbox ----
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

    // ---- dropdown / combo (/FT /Ch + combo flag) ----
    QPDFObjectHandle dd = QPDFObjectHandle::newDictionary();
    dd.replaceKey("/Type",    QPDFObjectHandle::newName("/Annot"));
    dd.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    dd.replaceKey("/FT",      QPDFObjectHandle::newName("/Ch"));
    dd.replaceKey("/Ff",      QPDFObjectHandle::newInteger(131072)); // combo
    dd.replaceKey("/T",       QPDFObjectHandle::newString("color"));
    dd.replaceKey("/V",       QPDFObjectHandle::newString("Green"));
    QPDFObjectHandle opts = QPDFObjectHandle::newArray();
    for (const char* s : {"Red", "Green", "Blue"})
        opts.appendItem(QPDFObjectHandle::newString(s));
    dd.replaceKey("/Opt",  opts);
    dd.replaceKey("/Rect", makeRect(72, 640, 288, 664));
    dd.replaceKey("/P",    pageObj);
    QPDFObjectHandle ddObj = pdf.makeIndirectObject(dd);

    // ---- wire /Annots on the page ----
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(tfObj);
    annots.appendItem(cbObj);
    annots.appendItem(ddObj);
    pageDict.replaceKey("/Annots", annots);

    // ---- /Pages ----
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageObj);
    QPDFObjectHandle pagesDict = QPDFObjectHandle::newDictionary();
    pagesDict.replaceKey("/Type",  QPDFObjectHandle::newName("/Pages"));
    pagesDict.replaceKey("/Kids",  kids);
    pagesDict.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle pagesObj = pdf.makeIndirectObject(pagesDict);
    pageDict.replaceKey("/Parent", pagesObj);

    // ---- AcroForm ----
    QPDFObjectHandle acroFields = QPDFObjectHandle::newArray();
    acroFields.appendItem(tfObj);
    acroFields.appendItem(cbObj);
    acroFields.appendItem(ddObj);
    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    acroForm.replaceKey("/Fields",         acroFields);
    acroForm.replaceKey("/NeedAppearances", QPDFObjectHandle::newBool(true));

    // ---- Catalog ----
    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages",   pagesObj);
    root.replaceKey("/AcroForm", acroForm);

    QPDFWriter writer(pdf, path.toStdString().c_str());
    writer.write();
    return path;
}

}  // namespace

class TestPdfEditor : public QObject {
    Q_OBJECT
private slots:
    void reportsInvalidForMissingFile();
    void loadsPageCountFromDisk();
    void saveRoundTripsPreservingPageCount();
    void deletePagesReducesCount();
    void movePageReordersPages();
    void insertPagesFromCombinesDocuments();
    void extractPagesWritesSubsetPdf();
    void cropPageSetsCropBox();
    void cropPageRejectsOversizedMargins();
    void annotationRoundTripPreservesShapes();
    void annotationRoundTripPreservesMarkup();
    void saveWithPasswordGatesLoad();
    void saveWithPasswordEmptyOwnerUsesUserPassword();
    void unlockRecoversLoadedButLockedEditor();

    // Form field tests (Phase 5)
    void formFieldsRoundTripsText();
    void formFieldsRoundTripsCheckbox();
    void formFieldsRoundTripsDropdown();
    void setFormFieldValuePersists();
};

void TestPdfEditor::reportsInvalidForMissingFile() {
    PdfEditor editor;
    QVERIFY(!editor.load("/tmp/does-not-exist.pdf"));
    QVERIFY(!editor.isValid());
    QCOMPARE(editor.pageCount(), 0);
}

void TestPdfEditor::loadsPageCountFromDisk() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeSamplePdf(dir.filePath("three.pdf"), 3);

    PdfEditor editor;
    QVERIFY(editor.load(path));
    QCOMPARE(editor.pageCount(), 3);
}

void TestPdfEditor::saveRoundTripsPreservingPageCount() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);
    const QString dst = dir.filePath("dst.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 2);
}

void TestPdfEditor::deletePagesReducesCount() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 4);
    const QString dst = dir.filePath("dst.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    editor.deletePages({0, 2});
    QCOMPARE(editor.pageCount(), 2);
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 2);
}

void TestPdfEditor::movePageReordersPages() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 3);
    const QString dst = dir.filePath("dst.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    editor.movePage(0, 2);
    QCOMPARE(editor.pageCount(), 3);
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 3);
}

void TestPdfEditor::insertPagesFromCombinesDocuments() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = writeSamplePdf(dir.filePath("base.pdf"), 2);
    const QString extra = writeSamplePdf(dir.filePath("extra.pdf"), 3);
    const QString dst = dir.filePath("combined.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(base));
    QVERIFY(editor.insertPagesFrom(extra, 1));
    QCOMPARE(editor.pageCount(), 5);
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 5);
}

void TestPdfEditor::extractPagesWritesSubsetPdf() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 5);
    const QString dst = dir.filePath("subset.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QVERIFY(editor.extractPages({1, 3}, dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 2);
}

void TestPdfEditor::cropPageSetsCropBox() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("cropped.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QVERIFY(editor.cropPage(0, 20.0, 30.0, 20.0, 30.0));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 1);
}

void TestPdfEditor::cropPageRejectsOversizedMargins() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QVERIFY(!editor.cropPage(0, 10000.0, 0.0, 0.0, 0.0));
}

void TestPdfEditor::annotationRoundTripPreservesShapes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);
    const QString dst = dir.filePath("annotated.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    std::vector<Annotation> anns;
    {
        Annotation rect;
        rect.page = 0;
        rect.type = AnnotationType::Rectangle;
        rect.bounds = QRectF(50, 60, 120, 80);
        rect.style.stroke = QColor(200, 20, 20);
        rect.style.strokeWidth = 2.0;
        anns.push_back(rect);
    }
    {
        Annotation ell;
        ell.page = 1;
        ell.type = AnnotationType::Ellipse;
        ell.bounds = QRectF(100, 100, 80, 40);
        anns.push_back(ell);
    }
    {
        Annotation line;
        line.page = 0;
        line.type = AnnotationType::Line;
        line.bounds = QRectF(10, 10, 40, 40);
        line.points = {QPointF(10, 10), QPointF(50, 50)};
        anns.push_back(line);
    }
    {
        Annotation arrow;
        arrow.page = 0;
        arrow.type = AnnotationType::Arrow;
        arrow.bounds = QRectF(0, 0, 30, 30);
        arrow.points = {QPointF(0, 0), QPointF(30, 30)};
        anns.push_back(arrow);
    }
    {
        Annotation note;
        note.page = 1;
        note.type = AnnotationType::Note;
        note.bounds = QRectF(10, 10, 18, 18);
        note.text = "hello";
        anns.push_back(note);
    }

    QVERIFY(editor.writeAnnotations(anns));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto got = round.readAnnotations();
    QCOMPARE(got.size(), anns.size());

    int rects = 0, ells = 0, lines = 0, arrows = 0, notes = 0;
    for (const Annotation& a : got) {
        switch (a.type) {
            case AnnotationType::Rectangle: ++rects; break;
            case AnnotationType::Ellipse:   ++ells;  break;
            case AnnotationType::Line:      ++lines; break;
            case AnnotationType::Arrow:     ++arrows; break;
            case AnnotationType::Note:      ++notes; break;
            default: break;
        }
    }
    QCOMPARE(rects, 1);
    QCOMPARE(ells, 1);
    QCOMPARE(lines, 1);
    QCOMPARE(arrows, 1);
    QCOMPARE(notes, 1);

    for (const Annotation& a : got) {
        if (a.type == AnnotationType::Note) {
            QCOMPARE(a.text, QStringLiteral("hello"));
            QCOMPARE(a.page, 1);
        }
        if (a.type == AnnotationType::Line) {
            QCOMPARE(a.points.size(), size_t{2});
        }
    }
}

void TestPdfEditor::annotationRoundTripPreservesMarkup() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("markup.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation hl;
    hl.page = 0;
    hl.type = AnnotationType::Highlight;
    hl.bounds = QRectF(20, 40, 200, 20);
    hl.quads = {QRectF(20, 40, 100, 20), QRectF(120, 40, 100, 20)};
    QVERIFY(editor.writeAnnotations({hl}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto got = round.readAnnotations();
    QCOMPARE(got.size(), size_t{1});
    QCOMPARE(got[0].type, AnnotationType::Highlight);
    QCOMPARE(got[0].quads.size(), size_t{2});
}

// ---------- Encryption (Phase 5) ------------------------------------------

// Saving with a user password must produce a PDF that:
//   (a) refuses to load without a password (isEncrypted() flips to true);
//   (b) refuses with the wrong password;
//   (c) opens with the right password via unlock().
// This is the core round-trip guarantee the File > Export as Password-
// Protected PDF… menu action depends on.
void TestPdfEditor::saveWithPasswordGatesLoad() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("locked.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    EncryptionOptions enc;
    enc.userPassword = QStringLiteral("open-sesame");
    QVERIFY(editor.save(dst, enc));

    // Fresh editor, no password: must fail *and* report encrypted.
    PdfEditor probe;
    QVERIFY(!probe.load(dst));
    QVERIFY(probe.isEncrypted());

    QVERIFY(!probe.unlock(QStringLiteral("wrong")));
    QVERIFY(!probe.isValid());

    QVERIFY(probe.unlock(QStringLiteral("open-sesame")));
    QVERIFY(probe.isValid());
    QCOMPARE(probe.pageCount(), 1);
}

// When ownerPassword is empty, qpdf uses userPassword for both. That
// means a reader with userPassword can still do everything — the
// "permissions only matter if owner ≠ user" rule. This is the
// happiest path for the minimal UI (a single password field) and is
// what we'll bind to the upcoming dialog.
void TestPdfEditor::saveWithPasswordEmptyOwnerUsesUserPassword() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("single-password.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    EncryptionOptions enc;
    enc.userPassword = QStringLiteral("one-word");
    // leave ownerPassword empty on purpose
    QVERIFY(editor.save(dst, enc));

    PdfEditor probe;
    QVERIFY(!probe.load(dst));
    QVERIFY(probe.isEncrypted());
    QVERIFY(probe.unlock(QStringLiteral("one-word")));
}

// unlock() on an already-valid editor must be a no-op; on a fresh
// editor that never saw an encrypted file it must report no-op-false
// (nothing to unlock). Guards the UI flow where the password dialog
// is only shown when isEncrypted() is true after load().
void TestPdfEditor::unlockRecoversLoadedButLockedEditor() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plain = writeSamplePdf(dir.filePath("plain.pdf"), 1);

    PdfEditor editor;
    QVERIFY(editor.load(plain));
    QVERIFY(editor.isValid());
    QVERIFY(!editor.isEncrypted());

    // unlock on an already-valid editor should succeed (idempotent).
    QVERIFY(editor.unlock(QStringLiteral("irrelevant")));

    PdfEditor empty;
    // No load() — unlock has nothing to do and must say so.
    QVERIFY(!empty.unlock(QStringLiteral("anything")));
}

// ---------- Form fields (Phase 5) -----------------------------------------

// readFormFields() must return the text field's name, label, type, and value.
void TestPdfEditor::formFieldsRoundTripsText() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFormPdf(dir.filePath("form.pdf"));

    PdfEditor editor;
    QVERIFY(editor.load(path));
    QVERIFY(editor.hasFormFields());

    const auto fields = editor.readFormFields();
    QCOMPARE(static_cast<int>(fields.size()), 3);

    const FormField* tf = nullptr;
    for (const auto& f : fields) {
        if (f.name == QStringLiteral("fullname")) { tf = &f; break; }
    }
    QVERIFY(tf != nullptr);
    QCOMPARE(tf->type, FormFieldType::Text);
    QCOMPARE(tf->value, QStringLiteral("Alice"));
    QCOMPARE(tf->label, QStringLiteral("Full Name"));
}

// readFormFields() must recognise a checkbox and report "Yes" when checked.
void TestPdfEditor::formFieldsRoundTripsCheckbox() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFormPdf(dir.filePath("form.pdf"));

    PdfEditor editor;
    QVERIFY(editor.load(path));

    const auto fields = editor.readFormFields();
    const FormField* cb = nullptr;
    for (const auto& f : fields) {
        if (f.name == QStringLiteral("agree")) { cb = &f; break; }
    }
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->type, FormFieldType::Checkbox);
    QCOMPARE(cb->value, QStringLiteral("Yes"));
}

// readFormFields() must decode a combo-box: type Dropdown, options list,
// and the current value.
void TestPdfEditor::formFieldsRoundTripsDropdown() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFormPdf(dir.filePath("form.pdf"));

    PdfEditor editor;
    QVERIFY(editor.load(path));

    const auto fields = editor.readFormFields();
    const FormField* dd = nullptr;
    for (const auto& f : fields) {
        if (f.name == QStringLiteral("color")) { dd = &f; break; }
    }
    QVERIFY(dd != nullptr);
    QCOMPARE(dd->type, FormFieldType::Dropdown);
    QCOMPARE(dd->value, QStringLiteral("Green"));
    QCOMPARE(dd->options.size(), 3);
    QVERIFY(dd->options.contains(QStringLiteral("Red")));
    QVERIFY(dd->options.contains(QStringLiteral("Green")));
    QVERIFY(dd->options.contains(QStringLiteral("Blue")));
}

// setFormFieldValue() must persist a new text-field value across save/reload.
void TestPdfEditor::setFormFieldValuePersists() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeFormPdf(dir.filePath("form.pdf"));
    const QString dst = dir.filePath("form_filled.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    // Locate the "fullname" field and change its value.
    const auto fields = editor.readFormFields();
    int tfId = -1;
    for (const auto& f : fields) {
        if (f.name == QStringLiteral("fullname")) { tfId = f.id; break; }
    }
    QVERIFY(tfId >= 0);
    QVERIFY(editor.setFormFieldValue(tfId, QStringLiteral("Bob")));
    QVERIFY(editor.save(dst));

    // Reload and verify the new value was stored.
    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto roundFields = round.readFormFields();
    const FormField* tf = nullptr;
    for (const auto& f : roundFields) {
        if (f.name == QStringLiteral("fullname")) { tf = &f; break; }
    }
    QVERIFY(tf != nullptr);
    QCOMPARE(tf->value, QStringLiteral("Bob"));
}

QTEST_MAIN(TestPdfEditor)
#include "test_pdf_editor.moc"
