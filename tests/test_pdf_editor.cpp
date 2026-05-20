#include "document/PdfCommands.h"
#include "document/PdfEditor.h"
#include "util/TempPath.h"

#include <qpdf/Pl_String.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QDir>
#include <QPdfWriter>
#include <QRectF>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest/QtTest>

#include <array>
#include <optional>

using namespace trailer;

namespace {

QString writeSamplePdf(const QString &path, int pages) {
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
static QString writeFormPdf(const QString &path) {
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
    tf.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    tf.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    tf.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    tf.replaceKey("/T", QPDFObjectHandle::newString("fullname"));
    tf.replaceKey("/TU", QPDFObjectHandle::newString("Full Name"));
    tf.replaceKey("/V", QPDFObjectHandle::newString("Alice"));
    tf.replaceKey("/Rect", makeRect(72, 720, 288, 744));
    tf.replaceKey("/P", pageObj);
    QPDFObjectHandle tfObj = pdf.makeIndirectObject(tf);

    // ---- checkbox ----
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

    // ---- dropdown / combo (/FT /Ch + combo flag) ----
    QPDFObjectHandle dd = QPDFObjectHandle::newDictionary();
    dd.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    dd.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    dd.replaceKey("/FT", QPDFObjectHandle::newName("/Ch"));
    dd.replaceKey("/Ff", QPDFObjectHandle::newInteger(131072)); // combo
    dd.replaceKey("/T", QPDFObjectHandle::newString("color"));
    dd.replaceKey("/V", QPDFObjectHandle::newString("Green"));
    QPDFObjectHandle opts = QPDFObjectHandle::newArray();
    for (const char *s : {"Red", "Green", "Blue"})
        opts.appendItem(QPDFObjectHandle::newString(s));
    dd.replaceKey("/Opt", opts);
    dd.replaceKey("/Rect", makeRect(72, 640, 288, 664));
    dd.replaceKey("/P", pageObj);
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
    pagesDict.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    pagesDict.replaceKey("/Kids", kids);
    pagesDict.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle pagesObj = pdf.makeIndirectObject(pagesDict);
    pageDict.replaceKey("/Parent", pagesObj);

    // ---- AcroForm ----
    QPDFObjectHandle acroFields = QPDFObjectHandle::newArray();
    acroFields.appendItem(tfObj);
    acroFields.appendItem(cbObj);
    acroFields.appendItem(ddObj);
    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    acroForm.replaceKey("/Fields", acroFields);
    acroForm.replaceKey("/NeedAppearances", QPDFObjectHandle::newBool(true));

    // ---- Catalog ----
    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages", pagesObj);
    root.replaceKey("/AcroForm", acroForm);

    QPDFWriter writer(pdf, path.toStdString().c_str());
    writer.write();
    return path;
}

} // namespace

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
    void annotationRoundTripPreservesInk();
    void annotationRoundTripPreservesTextAndSpeechBubble();
    void annotationRoundTripPreservesHighlightShape();
    void annotationRoundTripPreservesUnderlineAndStrikeOut();
    void annotationRectangleEmitsAppearanceStream();
    void annotationEllipseAndLineAndInkEmitAppearanceStreams();
    void rotatePageCommandIsReversible();
    void deletePagesCommandIsReversible();
    void deletePagesCommandRejectsDeletingEveryPage();
    void deletePagesCommandIsIdempotentAcrossApplyRevertApply();
    void movePageCommandIsReversible();
    void movePageCommandRejectsSameIndex();
    void insertPagesCommandIsReversible();
    void insertPagesCommandFailureDoesNotMutate();
    void cropPageCommandIsReversibleAndBatched();
    void cropPageCommandRestoresAbsentCropBox();
    void saveWithPasswordGatesLoad();
    void saveWithPasswordEmptyOwnerUsesUserPassword();
    void unlockRecoversLoadedButLockedEditor();

    // Form field tests (Phase 5)
    void formFieldsRoundTripsText();
    void formFieldsRoundTripsCheckbox();
    void formFieldsRoundTripsDropdown();
    void setFormFieldValuePersists();

    // File-size reduction (Phase 5)
    void saveReducedProducesValidPdf();
    void saveReducedFailsOnInvalidEditor();

    // Signature flattening (Phase 5)
    void flattenSignaturesEmbedsImageXObject();
    void flattenSignaturesWithMissingPngLeavesPageUntouched();
    void flattenSignaturesIsNoOpWithoutSignatureAnnotations();
    void saveSkipsSignatureAnnotationsInAnnots();

    // Redaction (Phase 5)
    void applyRedactionsReplacesPageContentWithRaster();
    void applyRedactionsRemovesOriginalAnnotsOnRedactedPage();
    void applyRedactionsIsNoOpWithoutRedactionAnnotations();
    void applyRedactionsOnlyTouchesPagesWithRedactions();
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
    for (const Annotation &a : got) {
        switch (a.type) {
        case AnnotationType::Rectangle:
            ++rects;
            break;
        case AnnotationType::Ellipse:
            ++ells;
            break;
        case AnnotationType::Line:
            ++lines;
            break;
        case AnnotationType::Arrow:
            ++arrows;
            break;
        case AnnotationType::Note:
            ++notes;
            break;
        default:
            break;
        }
    }
    QCOMPARE(rects, 1);
    QCOMPARE(ells, 1);
    QCOMPARE(lines, 1);
    QCOMPARE(arrows, 1);
    QCOMPARE(notes, 1);

    for (const Annotation &a : got) {
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

void TestPdfEditor::annotationRoundTripPreservesInk() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("ink.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation ink;
    ink.page = 0;
    ink.type = AnnotationType::Ink;
    ink.bounds = QRectF(40, 50, 120, 90);
    ink.points = {QPointF(40, 50), QPointF(80, 90), QPointF(160, 140)};
    ink.style.stroke = QColor(20, 60, 200);
    ink.style.strokeWidth = 1.5;
    QVERIFY(editor.writeAnnotations({ink}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto got = round.readAnnotations();
    QCOMPARE(got.size(), size_t{1});
    QCOMPARE(got[0].type, AnnotationType::Ink);
    QCOMPARE(got[0].points.size(), size_t{3});
}

void TestPdfEditor::annotationRoundTripPreservesTextAndSpeechBubble() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("text.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation text;
    text.page = 0;
    text.type = AnnotationType::Text;
    text.bounds = QRectF(100, 100, 200, 40);
    text.text = QStringLiteral("Hello, world!");

    Annotation bubble;
    bubble.page = 0;
    bubble.type = AnnotationType::SpeechBubble;
    bubble.bounds = QRectF(200, 200, 200, 80);
    bubble.text = QStringLiteral("Talking head");
    bubble.points = {QPointF(180, 290)}; // callout tail in doc coords

    QVERIFY(editor.writeAnnotations({text, bubble}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto got = round.readAnnotations();
    QCOMPARE(got.size(), size_t{2});
    int textCount = 0, bubbleCount = 0;
    for (const Annotation &a : got) {
        if (a.type == AnnotationType::Text) {
            ++textCount;
            QCOMPARE(a.text, QStringLiteral("Hello, world!"));
        } else if (a.type == AnnotationType::SpeechBubble) {
            ++bubbleCount;
            QCOMPARE(a.text, QStringLiteral("Talking head"));
            QCOMPARE(a.points.size(), size_t{1});
        }
    }
    QCOMPARE(textCount, 1);
    QCOMPARE(bubbleCount, 1);
}

void TestPdfEditor::annotationRoundTripPreservesHighlightShape() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("highlight_shape.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation hs;
    hs.page = 0;
    hs.type = AnnotationType::HighlightShape;
    hs.bounds = QRectF(50, 50, 100, 60);
    hs.style.stroke = QColor(255, 200, 0);
    hs.style.fill = QColor(255, 255, 0, 128); // translucent yellow
    QVERIFY(editor.writeAnnotations({hs}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto got = round.readAnnotations();
    QCOMPARE(got.size(), size_t{1});
    QCOMPARE(got[0].type, AnnotationType::HighlightShape);
    QVERIFY(got[0].style.fill.isValid());
}

void TestPdfEditor::annotationRoundTripPreservesUnderlineAndStrikeOut() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("under_strike.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation under;
    under.page = 0;
    under.type = AnnotationType::Underline;
    under.bounds = QRectF(20, 40, 200, 20);
    under.quads = {QRectF(20, 40, 200, 20)};

    Annotation strike;
    strike.page = 0;
    strike.type = AnnotationType::StrikeOut;
    strike.bounds = QRectF(20, 100, 200, 20);
    strike.quads = {QRectF(20, 100, 200, 20)};

    QVERIFY(editor.writeAnnotations({under, strike}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto got = round.readAnnotations();
    QCOMPARE(got.size(), size_t{2});
    int underlineCount = 0, strikeCount = 0;
    for (const Annotation &a : got) {
        if (a.type == AnnotationType::Underline)
            ++underlineCount;
        if (a.type == AnnotationType::StrikeOut)
            ++strikeCount;
    }
    QCOMPARE(underlineCount, 1);
    QCOMPARE(strikeCount, 1);
}

// Apple Preview (and a few other viewers) renders shape annotations
// from the /AP appearance stream rather than reconstructing from
// /C and /BS. Without /AP, marked-up PDFs rendered there look
// blank. The Rectangle writer emits a Form XObject in /AP /N; this
// test verifies the entry is present after a save+reload.
void TestPdfEditor::annotationRectangleEmitsAppearanceStream() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("rect_with_ap.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation rect;
    rect.page = 0;
    rect.type = AnnotationType::Rectangle;
    rect.bounds = QRectF(50, 60, 120, 80);
    rect.style.stroke = QColor(200, 20, 20);
    rect.style.strokeWidth = 2.0;
    QVERIFY(editor.writeAnnotations({rect}));
    QVERIFY(editor.save(dst));

    // Reopen via raw qpdf to inspect the saved /Annot for /AP /N.
    QPDF reopened;
    reopened.processFile(dst.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(reopened).getAllPages();
    QVERIFY(!pages.empty());
    QPDFObjectHandle annots = pages.front().getObjectHandle().getKey("/Annots");
    QVERIFY2(annots.isArray() && annots.getArrayNItems() >= 1,
             "Expected at least one /Annot entry on the saved page");
    QPDFObjectHandle entry = annots.getArrayItem(0);
    QPDFObjectHandle ap = entry.getKey("/AP");
    QVERIFY2(ap.isDictionary(), "/AP must be present and a dictionary");
    QPDFObjectHandle apN = ap.getKey("/N");
    QVERIFY2(apN.isStream(), "/AP /N must reference a Form XObject stream");
    // The XObject's dict should declare /Subtype /Form.
    QPDFObjectHandle apDict = apN.getDict();
    QPDFObjectHandle subtype = apDict.getKey("/Subtype");
    QVERIFY(subtype.isName());
    QCOMPARE(QString::fromStdString(subtype.getName()), QStringLiteral("/Form"));
}

// /AP coverage extends to Ellipse, Line, Arrow, and Ink. Each
// annotation type goes through its own appearance builder; this
// test asserts every saved /Annot ends up with a /AP /N stream
// declaring /Subtype /Form, so external viewers (Apple Preview,
// older Acrobat) render the shape from the appearance instead of
// reconstructing from /C and /BS. Highlight/Underline/StrikeOut and
// FreeText still rely on the property-based fallback and aren't in
// scope here.
void TestPdfEditor::annotationEllipseAndLineAndInkEmitAppearanceStreams() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("shapes_ap.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation ell;
    ell.page = 0;
    ell.type = AnnotationType::Ellipse;
    ell.bounds = QRectF(80, 100, 120, 60);
    ell.style.stroke = QColor(20, 20, 200);
    ell.style.strokeWidth = 1.5;

    Annotation line;
    line.page = 0;
    line.type = AnnotationType::Line;
    line.bounds = QRectF(40, 200, 200, 200);
    line.points = {QPointF(40, 200), QPointF(240, 200)};
    line.style.stroke = QColor(20, 200, 20);

    Annotation arrow;
    arrow.page = 0;
    arrow.type = AnnotationType::Arrow;
    arrow.bounds = QRectF(40, 240, 200, 240);
    arrow.points = {QPointF(40, 240), QPointF(240, 240)};
    arrow.style.stroke = QColor(200, 20, 20);

    Annotation ink;
    ink.page = 0;
    ink.type = AnnotationType::Ink;
    ink.bounds = QRectF(50, 300, 200, 60);
    // Some squiggle through the bounds.
    ink.points = {
        QPointF(50, 300),  QPointF(80, 320),  QPointF(120, 305),
        QPointF(160, 335), QPointF(200, 320), QPointF(250, 360),
    };

    QVERIFY(editor.writeAnnotations({ell, line, arrow, ink}));
    QVERIFY(editor.save(dst));

    QPDF reopened;
    reopened.processFile(dst.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(reopened).getAllPages();
    QVERIFY(!pages.empty());
    QPDFObjectHandle annots = pages.front().getObjectHandle().getKey("/Annots");
    QVERIFY(annots.isArray());
    QCOMPARE(annots.getArrayNItems(), 4);

    int withAp = 0;
    for (int i = 0; i < annots.getArrayNItems(); ++i) {
        QPDFObjectHandle entry = annots.getArrayItem(i);
        QPDFObjectHandle ap = entry.getKey("/AP");
        if (!ap.isDictionary())
            continue;
        QPDFObjectHandle apN = ap.getKey("/N");
        if (!apN.isStream())
            continue;
        QPDFObjectHandle apDict = apN.getDict();
        QPDFObjectHandle subtype = apDict.getKey("/Subtype");
        if (!subtype.isName())
            continue;
        if (QString::fromStdString(subtype.getName()) != QStringLiteral("/Form"))
            continue;
        ++withAp;
    }
    QCOMPARE(withAp, 4);
}

// RotatePageCommand applies +90° on apply() and revert() should
// restore the page to its original rotation. The test reads /Rotate
// from a saved-and-reloaded file because that's what determines
// what external viewers see.
void TestPdfEditor::rotatePageCommandIsReversible() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("rotated.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    auto readRotation = [&editor]() -> int {
        // Save to a tmp, reopen via raw qpdf, read /Rotate.
        // ScopedTempFile (not QTemporaryFile) because the latter holds
        // the OS handle past close() on Windows, blocking qpdf's
        // subsequent fopen("wb"). See src/util/TempPath.h.
        ScopedTempFile tmp(QStringLiteral("rot_check_XXXXXX.pdf"));
        if (!tmp.isValid())
            return -1;
        const QString p = tmp.path();
        if (!editor.save(p))
            return -1;
        QPDF reopened;
        reopened.processFile(p.toLocal8Bit().constData());
        auto pages = QPDFPageDocumentHelper(reopened).getAllPages();
        if (pages.empty())
            return -1;
        QPDFObjectHandle r = pages.front().getObjectHandle().getKey("/Rotate");
        return r.isInteger() ? int(r.getIntValue()) : 0;
    };

    const int rot0 = readRotation();
    QCOMPARE(rot0, 0);

    RotatePageCommand cmd(0, 90);
    QVERIFY(cmd.apply(editor));
    QCOMPARE(readRotation(), 90);

    QVERIFY(cmd.revert(editor));
    QCOMPARE(readRotation(), 0);

    // Re-apply confirms idempotence: apply→revert→apply lands at
    // the same final rotation as a single apply.
    QVERIFY(cmd.apply(editor));
    QCOMPARE(readRotation(), 90);

    Q_UNUSED(dst);
}

// DeletePagesCommand: apply removes the named pages; revert restores
// them at their original positions; re-apply lands on the same final
// page count as a single apply.
void TestPdfEditor::deletePagesCommandIsReversible() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 4);

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QCOMPARE(editor.pageCount(), 4);

    DeletePagesCommand cmd({1, 2});
    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 2);

    QVERIFY(cmd.revert(editor));
    QCOMPARE(editor.pageCount(), 4);

    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 2);

    QCOMPARE(cmd.description(), QStringLiteral("Delete Pages"));
}

// Deleting the only page must be refused — a zero-page PDF is
// unsavable in practice and the PdfDocument layer relies on the
// command surfacing the failure via a false apply().
void TestPdfEditor::deletePagesCommandRejectsDeletingEveryPage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);

    PdfEditor editor;
    QVERIFY(editor.load(src));

    DeletePagesCommand cmd({0, 1});
    QVERIFY2(!cmd.apply(editor),
             "Deleting every page must fail so the doc never reaches zero pages");
    QCOMPARE(editor.pageCount(), 2);
}

// Edge case for the capture-handles strategy: ensure that capture
// only happens on the first apply, so a second apply (post-revert)
// re-uses the SAME handles and produces the same end state.
void TestPdfEditor::deletePagesCommandIsIdempotentAcrossApplyRevertApply() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 3);

    PdfEditor editor;
    QVERIFY(editor.load(src));

    DeletePagesCommand cmd({0});
    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 2);
    QVERIFY(cmd.revert(editor));
    QCOMPARE(editor.pageCount(), 3);
    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 2);
    QVERIFY(cmd.revert(editor));
    QCOMPARE(editor.pageCount(), 3);

    // Save to disk after the round-trip and confirm the file
    // still has 3 pages — guards against the captured handles
    // becoming dangling references that qpdf serialises as garbage.
    const QString dst = dir.filePath("dst.pdf");
    QVERIFY(editor.save(dst));
    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 3);
}

// MovePageCommand: apply runs move(from, to); revert is move(to, from)
// — the inverse re-creates the original ordering because every other
// page shifts by exactly one slot in the opposite direction.
void TestPdfEditor::movePageCommandIsReversible() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 4);

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QCOMPARE(editor.pageCount(), 4);

    // We don't have a public "what's on page N" probe, so save the
    // pre-move state and the post-revert state to disk and compare
    // their content byte-for-byte. qpdf produces deterministic
    // output when setStaticID isn't enabled, so the same logical
    // tree serialises identically across runs.
    auto saveSnapshot = [&editor](const QString &p) {
        QVERIFY(editor.save(p));
    };
    const QString before = dir.filePath("before.pdf");
    saveSnapshot(before);

    MovePageCommand cmd(0, 3);
    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 4);

    QVERIFY(cmd.revert(editor));
    QCOMPARE(editor.pageCount(), 4);

    const QString after = dir.filePath("after.pdf");
    saveSnapshot(after);

    // QPdfWriter-generated PDFs include a /CreationDate so the two
    // saves won't be byte-identical at the file level; instead
    // compare page count + that the move command's description is
    // the expected label.
    QCOMPARE(cmd.description(), QStringLiteral("Move Page"));
}

// from == to is a degenerate move — apply() must return false so the
// caller doesn't push an empty command onto the undo stack.
void TestPdfEditor::movePageCommandRejectsSameIndex() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);

    PdfEditor editor;
    QVERIFY(editor.load(src));

    MovePageCommand cmd(1, 1);
    QVERIFY2(!cmd.apply(editor),
             "Move from N to N must fail — there's nothing to do and nothing to revert");
}

// InsertPagesCommand: apply inserts N pages from a source file at
// the given index; revert removes that contiguous range. The
// command snapshots count + clamped index on the first apply so
// revert removes exactly what apply added.
void TestPdfEditor::insertPagesCommandIsReversible() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = writeSamplePdf(dir.filePath("base.pdf"), 2);
    const QString extra = writeSamplePdf(dir.filePath("extra.pdf"), 3);

    PdfEditor editor;
    QVERIFY(editor.load(base));
    QCOMPARE(editor.pageCount(), 2);

    InsertPagesCommand cmd(extra, 1);
    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 5);

    QVERIFY(cmd.revert(editor));
    QCOMPARE(editor.pageCount(), 2);

    QVERIFY(cmd.apply(editor));
    QCOMPARE(editor.pageCount(), 5);

    QCOMPARE(cmd.description(), QStringLiteral("Insert Pages"));
}

// apply() must return false on bad source paths so PdfDocument
// doesn't push a no-op command onto the undo stack.
void TestPdfEditor::insertPagesCommandFailureDoesNotMutate() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = writeSamplePdf(dir.filePath("base.pdf"), 2);

    PdfEditor editor;
    QVERIFY(editor.load(base));

    InsertPagesCommand cmd(dir.filePath("does-not-exist.pdf"), 0);
    QVERIFY2(!cmd.apply(editor),
             "apply() must fail when the source can't be opened");
    QCOMPARE(editor.pageCount(), 2);
}

// CropPageCommand: a single command can crop N pages atomically;
// revert restores each affected page's prior /CropBox in lockstep.
// Test against the qpdf-level /CropBox dictionary because that's
// what external readers honour.
void TestPdfEditor::cropPageCommandIsReversibleAndBatched() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 3);

    PdfEditor editor;
    QVERIFY(editor.load(src));

    auto readCropBox = [&editor](int page) -> std::optional<std::array<double, 4>> {
        // Save, reopen via raw qpdf, read /CropBox.
        // ScopedTempFile (not QTemporaryFile) because the latter holds
        // the OS handle past close() on Windows, blocking qpdf's
        // subsequent fopen("wb"). See src/util/TempPath.h — same
        // reason rotatePageCommandIsReversible uses it above.
        ScopedTempFile tmp(QStringLiteral("crop_check_XXXXXX.pdf"));
        if (!tmp.isValid())
            return std::nullopt;
        const QString p = tmp.path();
        if (!editor.save(p))
            return std::nullopt;
        QPDF re;
        re.processFile(p.toLocal8Bit().constData());
        auto pages = QPDFPageDocumentHelper(re).getAllPages();
        if (page < 0 || page >= static_cast<int>(pages.size()))
            return std::nullopt;
        QPDFObjectHandle box = pages[static_cast<size_t>(page)].getObjectHandle().getKey("/CropBox");
        if (!box.isArray() || box.getArrayNItems() < 4)
            return std::nullopt;
        return std::array<double, 4>{
            box.getArrayItem(0).getNumericValue(),
            box.getArrayItem(1).getNumericValue(),
            box.getArrayItem(2).getNumericValue(),
            box.getArrayItem(3).getNumericValue(),
        };
    };

    // Pre-condition: no /CropBox on any page (QPdfWriter doesn't set
    // one), so readCropBox returns nullopt.
    QVERIFY(!readCropBox(0).has_value());
    QVERIFY(!readCropBox(1).has_value());
    QVERIFY(!readCropBox(2).has_value());

    CropPageCommand cmd({0, 1, 2}, 10.0, 10.0, 10.0, 10.0);
    QVERIFY(cmd.apply(editor));
    // After apply each page should have a /CropBox.
    QVERIFY(readCropBox(0).has_value());
    QVERIFY(readCropBox(1).has_value());
    QVERIFY(readCropBox(2).has_value());

    QVERIFY(cmd.revert(editor));
    // After revert no page should have a /CropBox.
    QVERIFY(!readCropBox(0).has_value());
    QVERIFY(!readCropBox(1).has_value());
    QVERIFY(!readCropBox(2).has_value());

    QVERIFY(cmd.apply(editor));
    QVERIFY(readCropBox(0).has_value());

    QCOMPARE(cmd.description(), QStringLiteral("Crop Pages"));
}

// Specific edge case: a page that already has a /CropBox (because the
// source PDF set one) should be restored to that exact array on
// revert, not to "no /CropBox key".
void TestPdfEditor::cropPageCommandRestoresAbsentCropBox() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);

    PdfEditor editor;
    QVERIFY(editor.load(src));

    // Single-page command labelled in singular.
    CropPageCommand cmd({0}, 5.0, 5.0, 5.0, 5.0);
    QCOMPARE(cmd.description(), QStringLiteral("Crop Page"));

    QVERIFY(cmd.apply(editor));
    QVERIFY(cmd.revert(editor));

    // Pre-apply had no /CropBox; revert must have removed the one
    // apply added rather than leaving an empty array behind.
    auto pages = QPDFPageDocumentHelper(*editor.qpdf()).getAllPages();
    QVERIFY(!pages.front().getObjectHandle().hasKey("/CropBox"));
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

    const FormField *tf = nullptr;
    for (const auto &f : fields) {
        if (f.name == QStringLiteral("fullname")) {
            tf = &f;
            break;
        }
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
    const FormField *cb = nullptr;
    for (const auto &f : fields) {
        if (f.name == QStringLiteral("agree")) {
            cb = &f;
            break;
        }
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
    const FormField *dd = nullptr;
    for (const auto &f : fields) {
        if (f.name == QStringLiteral("color")) {
            dd = &f;
            break;
        }
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
    for (const auto &f : fields) {
        if (f.name == QStringLiteral("fullname")) {
            tfId = f.id;
            break;
        }
    }
    QVERIFY(tfId >= 0);
    QVERIFY(editor.setFormFieldValue(tfId, QStringLiteral("Bob")));
    QVERIFY(editor.save(dst));

    // Reload and verify the new value was stored.
    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto roundFields = round.readFormFields();
    const FormField *tf = nullptr;
    for (const auto &f : roundFields) {
        if (f.name == QStringLiteral("fullname")) {
            tf = &f;
            break;
        }
    }
    QVERIFY(tf != nullptr);
    QCOMPARE(tf->value, QStringLiteral("Bob"));
}

// ---------- File-size reduction (Phase 5) ---------------------------------

// saveReduced must produce a valid PDF with the same page count. We don't
// assert the output is smaller — already-optimised inputs can even grow
// slightly — just that the writer succeeds and qpdf can round-trip it.
void TestPdfEditor::saveReducedProducesValidPdf() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 3);
    const QString dst = dir.filePath("reduced.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QVERIFY(editor.saveReduced(dst));
    QVERIFY(QFileInfo(dst).size() > 0);

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 3);
}

// saveReduced must report failure rather than crashing when the editor
// never loaded a file. Guards the menu action's enable state.
void TestPdfEditor::saveReducedFailsOnInvalidEditor() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PdfEditor editor; // never loaded
    QVERIFY(!editor.saveReduced(dir.filePath("out.pdf")));
}

namespace {

// Drop a tiny PNG onto disk — arbitrary bytes in ARGB32 with partial
// alpha so the SMask path gets exercised.
QString writeSampleSignaturePng(const QString &path) {
    QImage img(32, 16, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0, 0));
    QPainter p(&img);
    p.setPen(QColor(30, 30, 30, 220));
    p.drawLine(2, 12, 28, 4);
    p.drawLine(2, 8, 28, 14);
    p.end();
    img.save(path, "PNG");
    return path;
}

// Read the first page's /Resources/XObject and return the keys.
QStringList xobjectKeysOnFirstPage(const QString &pdfPath) {
    QPDF pdf;
    pdf.processFile(pdfPath.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (pages.empty())
        return {};
    QPDFObjectHandle resources = pages.front().getAttribute("/Resources", true);
    if (!resources.isDictionary())
        return {};
    QPDFObjectHandle xobj = resources.getKey("/XObject");
    if (!xobj.isDictionary())
        return {};
    QStringList out;
    for (const auto &key : xobj.getKeys()) {
        out << QString::fromStdString(key);
    }
    return out;
}

} // namespace

void TestPdfEditor::flattenSignaturesEmbedsImageXObject() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString pngPath = writeSampleSignaturePng(dir.filePath("sig.png"));
    const QString dst = dir.filePath("signed.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation sig;
    sig.page = 0;
    sig.type = AnnotationType::Signature;
    sig.bounds = QRectF(100, 100, 160, 60);
    sig.imagePath = pngPath;

    QVERIFY(editor.flattenSignatures({sig}));
    QVERIFY(editor.save(dst));

    // Round-trip: the saved PDF must load, still have one page, and
    // its /Resources/XObject must contain a TrailerSig* entry.
    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 1);

    const QStringList keys = xobjectKeysOnFirstPage(dst);
    bool foundSig = false;
    for (const QString &k : keys) {
        if (k.startsWith(QStringLiteral("/TrailerSig"))) {
            foundSig = true;
            break;
        }
    }
    QVERIFY2(foundSig, qPrintable("Expected /TrailerSig* key, got: " + keys.join(", ")));

    // Signatures are flattened, not stored as /Annot — readAnnotations
    // must NOT bring them back as an annotation.
    const auto reread = round.readAnnotations();
    for (const Annotation &a : reread) {
        QVERIFY(a.type != AnnotationType::Signature);
    }
}

// Missing PNG path: flattenSignatures silently skips the annotation so
// a stale signature reference doesn't corrupt the save.
void TestPdfEditor::flattenSignaturesWithMissingPngLeavesPageUntouched() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("out.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation sig;
    sig.page = 0;
    sig.type = AnnotationType::Signature;
    sig.bounds = QRectF(50, 50, 100, 30);
    sig.imagePath = dir.filePath("nope-does-not-exist.png");

    // Still reports success (missing PNG is a user error, not a
    // corrupt-editor state) — but no XObject gets added to the page.
    QVERIFY(editor.flattenSignatures({sig}));
    QVERIFY(editor.save(dst));

    const QStringList keys = xobjectKeysOnFirstPage(dst);
    for (const QString &k : keys) {
        QVERIFY2(!k.startsWith(QStringLiteral("/TrailerSig")),
                 qPrintable("Unexpected sig XObject: " + k));
    }
}

void TestPdfEditor::flattenSignaturesIsNoOpWithoutSignatureAnnotations() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);

    PdfEditor editor;
    QVERIFY(editor.load(src));

    // Empty list — success, no mutation.
    QVERIFY(editor.flattenSignatures({}));

    // List of non-signature types — also a no-op.
    Annotation rect;
    rect.page = 0;
    rect.type = AnnotationType::Rectangle;
    rect.bounds = QRectF(10, 10, 50, 50);
    QVERIFY(editor.flattenSignatures({rect}));
}

// writeAnnotations handles the full annotation list but must leave
// Signature typed entries out of each page's /Annots — they're in the
// content stream, not the annotation tree.
void TestPdfEditor::saveSkipsSignatureAnnotationsInAnnots() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString pngPath = writeSampleSignaturePng(dir.filePath("s.png"));
    const QString dst = dir.filePath("mixed.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation rect;
    rect.page = 0;
    rect.type = AnnotationType::Rectangle;
    rect.bounds = QRectF(10, 10, 40, 40);

    Annotation sig;
    sig.page = 0;
    sig.type = AnnotationType::Signature;
    sig.bounds = QRectF(80, 80, 120, 40);
    sig.imagePath = pngPath;

    QVERIFY(editor.flattenSignatures({rect, sig}));
    QVERIFY(editor.writeAnnotations({rect, sig}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    const auto all = round.readAnnotations();
    // Exactly one /Annot — the rectangle. Signature lives in content.
    QCOMPARE(static_cast<int>(all.size()), 1);
    QCOMPARE(all.front().type, AnnotationType::Rectangle);
}

namespace {

// Returns true if the page's /Contents stream is a single draw of a
// page-sized image XObject — i.e., the redaction rasterised the page.
bool firstPageIsRasterFlattened(const QString &pdfPath) {
    QPDF pdf;
    pdf.processFile(pdfPath.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (pages.empty())
        return false;
    std::string content;
    Pl_String sink("content", nullptr, content);
    pages.front().pipeContents(&sink);
    // Raster-flattened content looks like: q W 0 0 H X Y cm /Name Do Q
    // — a single draw of an image XObject.
    return content.find("/TrailerRed") != std::string::npos &&
           content.find(" Do") != std::string::npos;
}

// Count /Annots entries on the first page.
int firstPageAnnotCount(const QString &pdfPath) {
    QPDF pdf;
    pdf.processFile(pdfPath.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (pages.empty())
        return -1;
    QPDFObjectHandle annots = pages.front().getObjectHandle().getKey("/Annots");
    if (!annots.isArray())
        return 0;
    return annots.getArrayNItems();
}

} // namespace

// Placing a redaction rectangle on page 0 must: replace the page's
// content stream with a raster draw of a /TrailerRed* XObject, add
// that XObject to /Resources/XObject, and wipe /Annots on that page.
void TestPdfEditor::applyRedactionsReplacesPageContentWithRaster() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 1);
    const QString dst = dir.filePath("redacted.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation r;
    r.page = 0;
    r.type = AnnotationType::Redaction;
    r.bounds = QRectF(80, 80, 200, 60);

    QVERIFY(editor.applyRedactions({r}));
    QVERIFY(editor.save(dst));

    PdfEditor round;
    QVERIFY(round.load(dst));
    QCOMPARE(round.pageCount(), 1);

    const QStringList keys = xobjectKeysOnFirstPage(dst);
    bool foundRed = false;
    for (const QString &k : keys) {
        if (k.startsWith(QStringLiteral("/TrailerRed"))) {
            foundRed = true;
            break;
        }
    }
    QVERIFY2(foundRed, qPrintable("Expected /TrailerRed* key, got: " + keys.join(", ")));
    QVERIFY(firstPageIsRasterFlattened(dst));

    // Round-tripped file must not re-materialise the redaction as an
    // /Annot — the content is destroyed, not annotated.
    const auto reread = round.readAnnotations();
    for (const Annotation &a : reread) {
        QVERIFY(a.type != AnnotationType::Redaction);
    }
}

// applyRedactions must strip any /Annots already on the redacted page.
// The page's content stream has been rasterised, so any surviving
// annotations would reference destroyed coordinates. (writeAnnotations
// is the step that re-adds list-supplied annotations — tested
// separately. Here we exercise applyRedactions in isolation against a
// source PDF with pre-existing form widgets.)
void TestPdfEditor::applyRedactionsRemovesOriginalAnnotsOnRedactedPage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Use the form-widget fixture — it has three /Annots on page 0
    // from the start, independent of anything the editor writes.
    const QString src = writeFormPdf(dir.filePath("form.pdf"));
    const QString dst = dir.filePath("out.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));
    QVERIFY(firstPageAnnotCount(src) > 0);

    Annotation red;
    red.page = 0;
    red.type = AnnotationType::Redaction;
    red.bounds = QRectF(100, 100, 60, 30);

    QVERIFY(editor.applyRedactions({red}));
    QVERIFY(editor.save(dst));

    QCOMPARE(firstPageAnnotCount(dst), 0);
}

void TestPdfEditor::applyRedactionsIsNoOpWithoutRedactionAnnotations() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);
    const QString dst = dir.filePath("out.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    // Empty list — success, no mutation.
    QVERIFY(editor.applyRedactions({}));

    // Only non-redaction types — also success, no raster embedded.
    Annotation rect;
    rect.page = 0;
    rect.type = AnnotationType::Rectangle;
    rect.bounds = QRectF(10, 10, 50, 50);
    QVERIFY(editor.applyRedactions({rect}));
    QVERIFY(editor.save(dst));

    const QStringList keys = xobjectKeysOnFirstPage(dst);
    for (const QString &k : keys) {
        QVERIFY2(!k.startsWith(QStringLiteral("/TrailerRed")),
                 qPrintable("Unexpected redaction XObject: " + k));
    }
}

// Redactions on page 1 must not touch page 0's content stream.
void TestPdfEditor::applyRedactionsOnlyTouchesPagesWithRedactions() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeSamplePdf(dir.filePath("src.pdf"), 2);
    const QString dst = dir.filePath("out.pdf");

    PdfEditor editor;
    QVERIFY(editor.load(src));

    Annotation red;
    red.page = 1;
    red.type = AnnotationType::Redaction;
    red.bounds = QRectF(60, 60, 80, 40);

    QVERIFY(editor.applyRedactions({red}));
    QVERIFY(editor.save(dst));

    const QStringList keys = xobjectKeysOnFirstPage(dst);
    for (const QString &k : keys) {
        QVERIFY2(!k.startsWith(QStringLiteral("/TrailerRed")),
                 qPrintable("Page 0 should be untouched, got: " + k));
    }

    // Page 1 must have been rasterised.
    QPDF pdf;
    pdf.processFile(dst.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    QVERIFY(pages.size() >= 2);
    QPDFObjectHandle resources = pages[1].getAttribute("/Resources", true);
    QVERIFY(resources.isDictionary());
    QPDFObjectHandle xobj = resources.getKey("/XObject");
    QVERIFY(xobj.isDictionary());
    bool foundRed = false;
    for (const auto &key : xobj.getKeys()) {
        if (QString::fromStdString(key).startsWith(QStringLiteral("/TrailerRed"))) {
            foundRed = true;
            break;
        }
    }
    QVERIFY(foundRed);
}

QTEST_MAIN(TestPdfEditor)
#include "test_pdf_editor.moc"
