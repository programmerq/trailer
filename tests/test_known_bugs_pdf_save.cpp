// Known bugs in PDF Save and in undo across a Save. Each test asserts the
// correct behaviour; QEXPECT_FAIL marks each check that fails today.
#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <string>
#include <vector>

using namespace trailer;

namespace {

// Page i (0-based) of a fixture is firstWidth + 20 * i points wide, so each
// page can be recognised by its width after pages are deleted, moved or
// inserted.
QString writePdf(const QString &path, int pages, int firstWidth = 400) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter;
    for (int i = 0; i < pages; ++i) {
        writer.setPageSize(QPageSize(QSizeF(firstWidth + 20 * i, 600), QPageSize::Point, QString(),
                                     QPageSize::ExactMatch));
        if (i == 0)
            painter.begin(&writer);
        else
            writer.newPage();
        painter.drawText(QRectF(40, 40, 300, 40), QStringLiteral("Page %1").arg(i + 1));
    }
    painter.end();
    return path;
}

// Copies `in` to `out`, giving page 1 a /CropBox `left` points in from the
// MediaBox's left edge and `top` points down from its top edge.
bool writeCropped(const QString &in, const QString &out, double left, double top) {
    try {
        QPDF pdf;
        pdf.processFile(in.toLocal8Bit().constData());
        QPDFPageObjectHelper page = QPDFPageDocumentHelper(pdf).getAllPages().at(0);
        const QPDFObjectHandle::Rectangle media = page.getMediaBox().getArrayAsRectangle();
        page.getObjectHandle().replaceKey(
            "/CropBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(
                            media.llx + left, media.lly, media.urx, media.ury - top)));
        const QByteArray name = out.toLocal8Bit();
        QPDFWriter writer(pdf, name.constData());
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

QList<int> pageWidthList(const QString &path) {
    QList<int> widths;
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return widths;
    for (int i = 0; i < pdf.pageCount(); ++i)
        widths << qRound(pdf.pagePointSize(i).width());
    return widths;
}

QList<int> pageWidthList(const PdfDocument &doc) {
    QList<int> widths;
    for (int i = 0; i < doc.pageCount(); ++i)
        widths << qRound(doc.pageSizeHint(i).width());
    return widths;
}

QString joined(const QList<int> &values) {
    QStringList parts;
    for (int v : values)
        parts << QString::number(v);
    return parts.join(QLatin1Char(' '));
}

// Page widths in page order, e.g. "400 420 440": of a saved file, or of the
// pages the document shows now.
QString pageWidths(const QString &path) {
    return joined(pageWidthList(path));
}
QString pageWidths(const PdfDocument &doc) {
    return joined(pageWidthList(doc));
}

// Page 1's width and the number of marks, e.g. "600 wide, 1 mark". On the
// 400 x 600 fixture, 600 wide means page 1 is rotated a quarter turn.
QString pageAndMarks(PdfDocument &doc) {
    const int marks = doc.annotations()->count();
    return QStringLiteral("%1 wide, %2 mark%3")
        .arg(qRound(doc.pageSizeHint(0).width()))
        .arg(marks)
        .arg(marks == 1 ? QString() : QStringLiteral("s"));
}

Annotation rectMark(int page, const QRectF &bounds) {
    Annotation a;
    a.page = page;
    a.type = AnnotationType::Rectangle;
    a.bounds = bounds;
    a.style.stroke = QColor(255, 0, 0);
    a.style.strokeWidth = 2.0;
    return a;
}

// The marks Trailer reads back from the file at `path`.
std::vector<Annotation> marksInFile(const QString &path) {
    PdfEditor editor;
    if (!editor.load(path))
        return {};
    return editor.readAnnotations();
}

// Where each mark sits, as "x<left edge>@<width of its page>", sorted. Marks
// have distinct left edges and pages distinct widths, so this names each mark
// and its page whatever order the pages are in. "@-" is a page that does not
// exist.
QString placement(const std::vector<Annotation> &marks, const QList<int> &widths) {
    QStringList out;
    for (const Annotation &a : marks) {
        const QString page = a.page >= 0 && a.page < widths.size()
                                 ? QString::number(widths.at(a.page))
                                 : QStringLiteral("-");
        out << QStringLiteral("x%1@%2").arg(qRound(a.bounds.left())).arg(page);
    }
    out.sort();
    return out.join(QStringLiteral(", "));
}

// Marks on pages 1 and 3 of a 3-page fixture.
void addMarksOnFirstAndLastPage(PdfDocument &doc) {
    doc.annotations()->add(rectMark(0, QRectF(50, 50, 40, 30)));
    doc.annotations()->add(rectMark(2, QRectF(150, 50, 40, 30)));
}

QPDFObjectHandle pageAnnots(QPDF &pdf, int page) {
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (page < 0 || page >= static_cast<int>(pages.size()))
        return QPDFObjectHandle::newNull();
    return pages[static_cast<size_t>(page)].getObjectHandle().getKey("/Annots");
}

// Number of /Annots entries, of any subtype, on page `page` of the file.
int annotationCount(const QString &path, int page) {
    try {
        QPDF pdf;
        pdf.processFile(path.toLocal8Bit().constData());
        QPDFObjectHandle annots = pageAnnots(pdf, page);
        return annots.isArray() ? annots.getArrayNItems() : 0;
    } catch (const std::exception &) {
        return -1;
    }
}

// Page 1 carries markup another app wrote: a Highlight with author, id, dates
// and a Popup; a reply note pointing at it (/IRT); a dashed Square at 50%
// opacity; a FreeText with rich text; and a Link. Page 2 is blank.
QString writeForeignAnnotatedPdf(const QString &path) {
    QPDF pdf;
    pdf.emptyPDF();
    const auto indirect = [&pdf](const std::string &object) {
        return pdf.makeIndirectObject(QPDFObjectHandle::parse(object));
    };
    QPDFObjectHandle highlight =
        indirect("<< /Type /Annot /Subtype /Highlight /Rect [70 695 400 725]"
                 " /QuadPoints [72 722 398 722 72 698 398 698] /C [1 1 0] /CA 0.6"
                 " /T (Alice Reviewer) /NM (hl-1) /Contents (Check this number)"
                 " /Subj (Highlight) /CreationDate (D:20250101120000Z)"
                 " /M (D:20250102120000Z) /F 4 >>");
    QPDFObjectHandle popup =
        indirect("<< /Type /Annot /Subtype /Popup /Rect [420 600 600 700] /Open false /F 28 >>");
    popup.replaceKey("/Parent", highlight);
    highlight.replaceKey("/Popup", popup);
    QPDFObjectHandle reply =
        indirect("<< /Type /Annot /Subtype /Text /Rect [500 740 520 760] /Name /Comment"
                 " /C [0 0 1] /T (Bob Lawyer) /NM (note-2) /Contents (Reply: agreed)"
                 " /RT /R /F 4 >>");
    reply.replaceKey("/IRT", highlight);
    QPDFObjectHandle square =
        indirect("<< /Type /Annot /Subtype /Square /Rect [100 300 300 450] /C [0 0.5 0]"
                 " /IC [0.9 0.9 0.5] /BS << /W 3 /S /D /D [6 3] >> /CA 0.5 /T (Carol)"
                 " /NM (sq-3) /Contents (Region of interest) /F 4 >>");
    QPDFObjectHandle freeText =
        indirect("<< /Type /Annot /Subtype /FreeText /Rect [100 500 400 560]"
                 " /DA (/Helv 14 Tf 1 0 0 rg) /Contents (Typewriter note)"
                 " /RC (<body xmlns=\"http://www.w3.org/1999/xhtml\"><p>Typewriter note</p>"
                 "</body>) /T (Dave) /NM (ft-4) /F 4 >>");
    QPDFObjectHandle link =
        indirect("<< /Type /Annot /Subtype /Link /Rect [72 650 300 670]"
                 " /Border [0 0 0] /A << /S /URI /URI (https://example.com) >> >>");

    QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
    fonts.replaceKey("/F1", indirect("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fonts);
    QPDFPageDocumentHelper pages(pdf);
    for (int i = 0; i < 2; ++i) {
        QPDFObjectHandle page = indirect("<< /Type /Page /MediaBox [0 0 612 792] >>");
        page.replaceKey("/Resources", resources);
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(
                                         &pdf, "BT /F1 24 Tf 72 740 Td (Reviewed) Tj ET"));
        if (i == 0) {
            page.replaceKey("/Annots", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{
                                           highlight, popup, reply, square, link, freeText}));
        }
        pages.addPage(QPDFPageObjectHelper(page), /*first=*/false);
    }
    const QByteArray file = path.toLocal8Bit();
    QPDFWriter writer(pdf, file.constData());
    writer.write();
    return path;
}

QString numbers(QPDFObjectHandle array) {
    QStringList parts;
    for (QPDFObjectHandle &item : array.getArrayAsVector())
        parts << (item.isNumber() ? QString::number(item.getNumericValue()) : QStringLiteral("?"));
    return parts.join(QLatin1Char(' '));
}

// An annotation's /NM, or its /Subtype when it has none.
QString nameOf(QPDFObjectHandle a) {
    QPDFObjectHandle nm = a.getKey("/NM");
    if (nm.isString())
        return QString::fromStdString(nm.getUTF8Value());
    QPDFObjectHandle subtype = a.getKey("/Subtype");
    return subtype.isName() ? QString::fromStdString(subtype.getName()) : QStringLiteral("?");
}

QString describeAnnotation(QPDFObjectHandle a) {
    QStringList parts;
    QPDFObjectHandle subtype = a.getKey("/Subtype");
    parts << (subtype.isName() ? QString::fromStdString(subtype.getName()) : QStringLiteral("?"));
    for (const char *key : {"/NM", "/T", "/Subj", "/CreationDate", "/RC"}) {
        QPDFObjectHandle value = a.getKey(key);
        if (value.isString())
            parts << QStringLiteral("%1=%2").arg(QLatin1String(key),
                                                 QString::fromStdString(value.getUTF8Value()));
    }
    QPDFObjectHandle opacity = a.getKey("/CA");
    if (opacity.isNumber())
        parts << QStringLiteral("/CA=%1").arg(opacity.getNumericValue());
    QPDFObjectHandle quads = a.getKey("/QuadPoints");
    if (quads.isArray())
        parts << QStringLiteral("/QuadPoints=%1").arg(numbers(quads));
    QPDFObjectHandle border = a.getKey("/BS");
    if (border.isDictionary() && border.getKey("/D").isArray())
        parts << QStringLiteral("/BS/D=%1").arg(numbers(border.getKey("/D")));
    for (const char *key : {"/IRT", "/Popup", "/Parent"}) {
        QPDFObjectHandle target = a.getKey(key);
        if (target.isDictionary())
            parts << QStringLiteral("%1->%2").arg(QLatin1String(key), nameOf(target));
    }
    QPDFObjectHandle action = a.getKey("/A");
    if (action.isDictionary() && action.getKey("/URI").isString())
        parts << QStringLiteral("/URI=%1").arg(
            QString::fromStdString(action.getKey("/URI").getUTF8Value()));
    return parts.join(QLatin1Char(' '));
}

// The keys that matter on the other app's annotations (each one with an /NM,
// plus the Link and the Popup) on page 1 of the file at `path`, sorted.
QString foreignAnnotationKeys(const QString &path) {
    try {
        QPDF pdf;
        pdf.processFile(path.toLocal8Bit().constData());
        QPDFObjectHandle annots = pageAnnots(pdf, 0);
        if (!annots.isArray())
            return {};
        QStringList lines;
        for (QPDFObjectHandle &a : annots.getArrayAsVector()) {
            if (!a.isDictionary())
                continue;
            if (a.getKey("/NM").isString() || a.getKey("/Subtype").isNameAndEquals("/Link") ||
                a.getKey("/Subtype").isNameAndEquals("/Popup"))
                lines << describeAnnotation(a);
        }
        lines.sort();
        return lines.join(QStringLiteral(" | "));
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
}

const QString kForeignKeys =
    QStringList{
        QStringLiteral(
            "/FreeText /NM=ft-4 /T=Dave /RC=<body xmlns=\"http://www.w3.org/1999/xhtml\">"
            "<p>Typewriter note</p></body>"),
        QStringLiteral("/Highlight /NM=hl-1 /T=Alice Reviewer /Subj=Highlight"
                       " /CreationDate=D:20250101120000Z /CA=0.6"
                       " /QuadPoints=72 722 398 722 72 698 398 698 /Popup->/Popup"),
        QStringLiteral("/Link /URI=https://example.com"),
        QStringLiteral("/Popup /Parent->hl-1"),
        QStringLiteral("/Square /NM=sq-3 /T=Carol /CA=0.5 /BS/D=6 3"),
        QStringLiteral("/Text /NM=note-2 /T=Bob Lawyer /IRT->hl-1"),
    }
        .join(QStringLiteral(" | "));

// Bounding box, in points, of the red pixels pdfium paints on page `page` of
// the file at `path` with annotations on. Empty when nothing red is painted.
QRectF paintedRedBox(const QString &path, int page) {
    constexpr int kScale = 2; // pixels per point
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return {};
    QPdfDocumentRenderOptions options;
    options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    const QImage image = pdf.render(page, (pdf.pagePointSize(page) * kScale).toSize(), options)
                             .convertToFormat(QImage::Format_RGB32);
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qRed(line[x]) > 180 && qGreen(line[x]) < 90 && qBlue(line[x]) < 90)
                box |= QRect(x, y, 1, 1);
        }
    }
    return QRectF(box.x() / double(kScale), box.y() / double(kScale), box.width() / double(kScale),
                  box.height() / double(kScale));
}

QSize displayedPageSize(const QString &path, int page) {
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return {};
    return pdf.pagePointSize(page).toSize();
}

bool sameRect(const QRectF &a, const QRectF &b, double tolerance) {
    return qAbs(a.left() - b.left()) <= tolerance && qAbs(a.top() - b.top()) <= tolerance &&
           qAbs(a.right() - b.right()) <= tolerance && qAbs(a.bottom() - b.bottom()) <= tolerance;
}

QString rectText(const QRectF &r) {
    return QStringLiteral("(%1,%2 %3x%4)")
        .arg(r.x(), 0, 'f', 1)
        .arg(r.y(), 0, 'f', 1)
        .arg(r.width(), 0, 'f', 1)
        .arg(r.height(), 0, 'f', 1);
}

} // namespace

class TestKnownBugsPdfSave : public QObject {
    Q_OBJECT
  private slots:
    void successiveSavesWriteEachMarkOnce_data();
    void successiveSavesWriteEachMarkOnce();
    void saveKeepsMarkCountOfAnnotatedFile_data();
    void saveKeepsMarkCountOfAnnotatedFile();
    void saveKeepsOtherAppsAnnotationsIntact();
    void deletePageTakesItsMarkAndKeepsOthersOnTheirPages();
    void movePageKeepsMarksOnTheirPages();
    void insertPageKeepsMarksOnTheirPages();
    void markOnRotatedOrCroppedPageSavesWhereDrawn_data();
    void markOnRotatedOrCroppedPageSavesWhereDrawn();
    void zoomLensSurvivesSave();
    void undoPageOperationAfterSaveRestoresPages_data();
    void undoPageOperationAfterSaveRestoresPages();
    void undoAfterSaveRevertsNewestFirst_data();
    void undoAfterSaveRevertsNewestFirst();
};

void TestKnownBugsPdfSave::successiveSavesWriteEachMarkOnce_data() {
    QTest::addColumn<bool>("saveAs");
    QTest::newRow("save") << false;
    QTest::newRow("save as") << true;
}

// todo/2026-10-15-save-reappends-annotations: PDF Save re-appends every annotation already in the file.
void TestKnownBugsPdfSave::successiveSavesWriteEachMarkOnce() {
    QFETCH(bool, saveAs);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("doc.pdf"), 1);
    PdfDocument doc(path);
    QVERIFY(doc.isValid());

    QStringList counts;
    for (int i = 0; i < 3; ++i) {
        doc.annotations()->add(rectMark(0, QRectF(50 + 100 * i, 50, 40, 30)));
        const QString target = saveAs ? dir.filePath(QStringLiteral("copy%1.pdf").arg(i)) : path;
        QVERIFY(doc.save(saveAs ? target : QString()));
        counts << QString::number(marksInFile(target).size());
    }
    QEXPECT_FAIL("save", "todo/2026-10-15-save-reappends-annotations: each Save re-appends the marks already in the file", Abort);
    QEXPECT_FAIL("save as", "todo/2026-10-15-save-reappends-annotations: each Save As re-appends the marks already in the file", Abort);
    QCOMPARE(counts.join(QLatin1Char(' ')), QStringLiteral("1 2 3"));
}

void TestKnownBugsPdfSave::saveKeepsMarkCountOfAnnotatedFile_data() {
    QTest::addColumn<bool>("rotate");
    QTest::addColumn<bool>("saveAs");
    QTest::newRow("clean save") << false << false;
    QTest::newRow("rotate and save") << true << false;
    QTest::newRow("clean save as") << false << true;
    QTest::newRow("rotate and save as") << true << true;
}

// todo/2026-10-15-save-reappends-annotations: PDF Save re-appends every annotation already in the file.
void TestKnownBugsPdfSave::saveKeepsMarkCountOfAnnotatedFile() {
    QFETCH(bool, rotate);
    QFETCH(bool, saveAs);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString blank = writePdf(dir.filePath("blank.pdf"), 2);
    const QString path = dir.filePath("annotated.pdf");
    {
        PdfEditor editor;
        QVERIFY(editor.load(blank));
        QVERIFY(editor.writeAnnotations(
            {rectMark(0, QRectF(50, 50, 40, 30)), rectMark(0, QRectF(150, 50, 40, 30))}));
        QVERIFY(editor.save(path));
    }
    QCOMPARE(int(marksInFile(path).size()), 2);

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    if (rotate) {
        doc.rotatePage(1, 90); // the page without marks
        QVERIFY(doc.isDirty());
    } else {
        QVERIFY(!doc.isDirty());
    }
    const QString target = saveAs ? dir.filePath("copy.pdf") : path;
    QVERIFY(doc.save(saveAs ? target : QString()));
    QEXPECT_FAIL("clean save", "todo/2026-10-15-save-reappends-annotations: a clean Save re-appends the marks already in the file",
                 Abort);
    QEXPECT_FAIL("rotate and save", "todo/2026-10-15-save-reappends-annotations: Save re-appends the marks already in the file", Abort);
    QEXPECT_FAIL("clean save as", "todo/2026-10-15-save-reappends-annotations: a clean Save As re-appends the marks already in the file",
                 Abort);
    QEXPECT_FAIL("rotate and save as", "todo/2026-10-15-save-reappends-annotations: Save As re-appends the marks already in the file",
                 Abort);
    QCOMPARE(int(marksInFile(target).size()), 2);
}

// todo/2026-10-15-save-reappends-annotations: PDF Save re-appends every annotation already in the file.
void TestKnownBugsPdfSave::saveKeepsOtherAppsAnnotationsIntact() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeForeignAnnotatedPdf(dir.filePath("reviewed.pdf"));
    QCOMPARE(annotationCount(path, 0), 6);
    QCOMPARE(foreignAnnotationKeys(path), kForeignKeys);

    // Three clean saves, then a mark on page 2 and a fourth save. After each,
    // the annotations on pages 1 and 2, e.g. "6+0".
    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QStringList counts;
    for (int save = 1; save <= 4; ++save) {
        if (save == 4) {
            doc.annotations()->add(rectMark(1, QRectF(300, 300, 40, 30)));
            QVERIFY(doc.isDirty());
        }
        QVERIFY(doc.save());
        // Authors, ids, dates, replies, popups, rich text, opacity and dashes stay.
        QCOMPARE(foreignAnnotationKeys(path), kForeignKeys);
        counts
            << QStringLiteral("%1+%2").arg(annotationCount(path, 0)).arg(annotationCount(path, 1));
    }
    QEXPECT_FAIL("", "todo/2026-10-15-save-reappends-annotations: each Save appends stripped copies of the other app's marks", Abort);
    QCOMPARE(counts.join(QLatin1Char(' ')), QStringLiteral("6+0 6+0 6+0 6+1"));
}

// todo/2026-10-15-page-edits-misplace-marks: Deleting, moving or inserting pages leaves marks on the wrong page.
void TestKnownBugsPdfSave::deletePageTakesItsMarkAndKeepsOthersOnTheirPages() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("three.pdf"), 3);
    PdfDocument doc(path);
    QCOMPARE(pageWidths(doc), QStringLiteral("400 420 440"));
    addMarksOnFirstAndLastPage(doc);

    doc.deletePages({0});
    QCOMPARE(pageWidths(doc), QStringLiteral("420 440"));
    // Page 1's mark goes with the page; page 3's mark stays on page 3, now page 2.
    QEXPECT_FAIL("", "todo/2026-10-15-page-edits-misplace-marks: deleting a page leaves the marks' page numbers unchanged", Continue);
    QCOMPARE(placement(doc.annotations()->annotations(), pageWidthList(doc)),
             QStringLiteral("x150@440"));
    // ⌘Z brings the page back with its mark.
    QVERIFY(doc.undo());
    QCOMPARE(pageWidths(doc), QStringLiteral("400 420 440"));
    QCOMPARE(placement(doc.annotations()->annotations(), pageWidthList(doc)),
             QStringLiteral("x150@440, x50@400"));

    doc.deletePages({0});
    QVERIFY(doc.save());
    QCOMPARE(pageWidths(path), QStringLiteral("420 440"));
    QEXPECT_FAIL("", "todo/2026-10-15-page-edits-misplace-marks: Save writes the marks to their old page numbers", Abort);
    QCOMPARE(placement(marksInFile(path), pageWidthList(path)), QStringLiteral("x150@440"));
}

// todo/2026-10-15-page-edits-misplace-marks: Deleting, moving or inserting pages leaves marks on the wrong page.
void TestKnownBugsPdfSave::movePageKeepsMarksOnTheirPages() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("three.pdf"), 3);
    PdfDocument doc(path);
    QCOMPARE(pageWidths(doc), QStringLiteral("400 420 440"));
    addMarksOnFirstAndLastPage(doc);

    // Page 3 moves to the front, which also shifts page 1 to second place.
    // todo/2026-10-15-page-drag-lands-early: no page can reach the end, where the bar moves page 1.
    doc.movePage(2, 0);
    QCOMPARE(pageWidths(doc), QStringLiteral("440 400 420"));
    QEXPECT_FAIL("", "todo/2026-10-15-page-edits-misplace-marks: moving a page leaves the marks' page numbers unchanged", Continue);
    QCOMPARE(placement(doc.annotations()->annotations(), pageWidthList(doc)),
             QStringLiteral("x150@440, x50@400"));
    // ⌘Z puts the page and the marks back.
    QVERIFY(doc.undo());
    QCOMPARE(pageWidths(doc), QStringLiteral("400 420 440"));
    QCOMPARE(placement(doc.annotations()->annotations(), pageWidthList(doc)),
             QStringLiteral("x150@440, x50@400"));

    doc.movePage(2, 0);
    QVERIFY(doc.save());
    QCOMPARE(pageWidths(path), QStringLiteral("440 400 420"));
    QEXPECT_FAIL("", "todo/2026-10-15-page-edits-misplace-marks: Save writes the marks to their old page numbers", Abort);
    QCOMPARE(placement(marksInFile(path), pageWidthList(path)),
             QStringLiteral("x150@440, x50@400"));
}

// todo/2026-10-15-page-edits-misplace-marks: Deleting, moving or inserting pages leaves marks on the wrong page.
void TestKnownBugsPdfSave::insertPageKeepsMarksOnTheirPages() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("three.pdf"), 3);
    const QString extra = writePdf(dir.filePath("extra.pdf"), 1, 300);
    PdfDocument doc(path);
    QCOMPARE(pageWidths(doc), QStringLiteral("400 420 440"));
    addMarksOnFirstAndLastPage(doc);

    QVERIFY(doc.insertPagesFrom(extra, 0));
    QCOMPARE(pageWidths(doc), QStringLiteral("300 400 420 440"));
    QEXPECT_FAIL("", "todo/2026-10-15-page-edits-misplace-marks: inserting a page leaves the marks' page numbers unchanged", Continue);
    QCOMPARE(placement(doc.annotations()->annotations(), pageWidthList(doc)),
             QStringLiteral("x150@440, x50@400"));
    // ⌘Z removes the inserted page and leaves the marks where they were.
    QVERIFY(doc.undo());
    QCOMPARE(pageWidths(doc), QStringLiteral("400 420 440"));
    QCOMPARE(placement(doc.annotations()->annotations(), pageWidthList(doc)),
             QStringLiteral("x150@440, x50@400"));

    QVERIFY(doc.insertPagesFrom(extra, 0));
    QVERIFY(doc.save());
    QCOMPARE(pageWidths(path), QStringLiteral("300 400 420 440"));
    QEXPECT_FAIL("", "todo/2026-10-15-page-edits-misplace-marks: Save writes the marks to their old page numbers", Abort);
    QCOMPARE(placement(marksInFile(path), pageWidthList(path)),
             QStringLiteral("x150@440, x50@400"));
}

void TestKnownBugsPdfSave::markOnRotatedOrCroppedPageSavesWhereDrawn_data() {
    QTest::addColumn<int>("degrees");
    QTest::addColumn<bool>("cropped");
    QTest::addColumn<QSize>("displayed");
    // Control: unrotated, uncropped pages save correctly today.
    QTest::newRow("0") << 0 << false << QSize(400, 600);
    QTest::newRow("90") << 90 << false << QSize(600, 400);
    QTest::newRow("180") << 180 << false << QSize(400, 600);
    QTest::newRow("270") << 270 << false << QSize(600, 400);
    // A CropBox 50 pt in from the left edge and 80 pt down from the top.
    QTest::newRow("cropped") << 0 << true << QSize(350, 520);
}

// todo/2026-10-15-rotated-page-marks-shift-on-save: Marks on rotated pages are saved away from where they were drawn.
void TestKnownBugsPdfSave::markOnRotatedOrCroppedPageSavesWhereDrawn() {
    QFETCH(int, degrees);
    QFETCH(bool, cropped);
    QFETCH(QSize, displayed);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = writePdf(dir.filePath("page.pdf"), 1);
    if (cropped) {
        const QString uncropped = path;
        path = dir.filePath("cropped.pdf");
        QVERIFY(writeCropped(uncropped, path, 50, 80));
    }
    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    if (degrees != 0)
        doc.rotatePage(0, degrees);
    // Near the visible top-left, in the coordinates of the page as displayed.
    const QRectF drawn(20, 20, 100, 60);
    Annotation mark = rectMark(0, drawn);
    mark.style.strokeWidth = 4.0;
    doc.annotations()->add(mark);
    QVERIFY(doc.save());

    QCOMPARE(displayedPageSize(path, 0), displayed);
    const std::vector<Annotation> reread = marksInFile(path);
    QCOMPARE(int(reread.size()), 1);
    QVERIFY2(sameRect(reread.front().bounds, drawn, 0.5),
             qPrintable(QStringLiteral("Trailer re-reads the mark at %1, drawn at %2")
                            .arg(rectText(reread.front().bounds), rectText(drawn))));
    const QRectF painted = paintedRedBox(path, 0);
    QEXPECT_FAIL("90", "todo/2026-10-15-rotated-page-marks-shift-on-save: Save ignores the page's /Rotate", Abort);
    QEXPECT_FAIL("180", "todo/2026-10-15-rotated-page-marks-shift-on-save: Save ignores the page's /Rotate", Abort);
    QEXPECT_FAIL("270", "todo/2026-10-15-rotated-page-marks-shift-on-save: Save ignores the page's /Rotate", Abort);
    QEXPECT_FAIL("cropped", "todo/2026-10-15-rotated-page-marks-shift-on-save: Save ignores the page's CropBox", Abort);
    QVERIFY2(sameRect(painted, drawn, 3.0),
             qPrintable(QStringLiteral("pdfium paints the mark at %1, drawn at %2")
                            .arg(rectText(painted), rectText(drawn))));
}

// todo/2026-10-15-zoom-lens-marks-lost-on-save: Zoom Lens marks vanish when a PDF is saved.
void TestKnownBugsPdfSave::zoomLensSurvivesSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("lens.pdf"), 1);
    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    Annotation lens;
    lens.page = 0;
    lens.type = AnnotationType::ZoomLens;
    lens.bounds = QRectF(100, 100, 80, 80);
    doc.annotations()->add(lens);
    QCOMPARE(doc.annotations()->count(), 1);
    QVERIFY(doc.save());

    QEXPECT_FAIL("", "todo/2026-10-15-zoom-lens-marks-lost-on-save: Save drops the Zoom Lens from the document", Continue);
    QCOMPARE(doc.annotations()->count(), 1);
    int lensesInFile = 0;
    for (const Annotation &a : marksInFile(path))
        lensesInFile += a.type == AnnotationType::ZoomLens ? 1 : 0;
    QEXPECT_FAIL("", "todo/2026-10-15-zoom-lens-marks-lost-on-save: Save does not write the Zoom Lens to the file", Abort);
    QCOMPARE(lensesInFile, 1);
}

void TestKnownBugsPdfSave::undoPageOperationAfterSaveRestoresPages_data() {
    QTest::addColumn<bool>("deletePage");
    QTest::addColumn<QString>("afterOperation");
    QTest::newRow("delete page 2") << true << QStringLiteral("400 440");
    QTest::newRow("move page 3 to the front") << false << QStringLiteral("440 400 420");
}

// todo/2026-10-15-page-op-undo-after-save: ⌘Z of a page operation after Save does nothing, yet marks the document edited.
void TestKnownBugsPdfSave::undoPageOperationAfterSaveRestoresPages() {
    QFETCH(bool, deletePage);
    QFETCH(QString, afterOperation);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("pages.pdf"), 3);
    const QString original = QStringLiteral("400 420 440");
    PdfDocument doc(path);
    QCOMPARE(pageWidths(doc), original);
    if (deletePage)
        doc.deletePages({1});
    else
        doc.movePage(2, 0);
    QCOMPARE(pageWidths(doc), afterOperation);
    QVERIFY(doc.save());
    QCOMPARE(pageWidths(path), afterOperation);
    QVERIFY(!doc.isDirty());
    QVERIFY(doc.canUndo());

    const bool undone = doc.undo();
    const bool restored = pageWidths(doc) == original;
    QEXPECT_FAIL("delete page 2", "todo/2026-10-15-page-op-undo-after-save: undo after Save does not bring the page back", Continue);
    QEXPECT_FAIL("move page 3 to the front", "todo/2026-10-15-page-op-undo-after-save: undo after Save does not move the page back",
                 Continue);
    QCOMPARE(pageWidths(doc), original);
    // Neither undo()'s result nor the edited flag may claim a change that did not happen.
    QEXPECT_FAIL("delete page 2", "todo/2026-10-15-page-op-undo-after-save: undo() reports success but reverted nothing", Continue);
    QEXPECT_FAIL("move page 3 to the front", "todo/2026-10-15-page-op-undo-after-save: undo() reports success but reverted nothing",
                 Continue);
    QVERIFY2(!undone || restored, "undo() returned true but the pages did not change");
    QEXPECT_FAIL("delete page 2", "todo/2026-10-15-page-op-undo-after-save: the undo that did nothing marks the document edited",
                 Continue);
    QEXPECT_FAIL("move page 3 to the front",
                 "todo/2026-10-15-page-op-undo-after-save: the undo that did nothing marks the document edited", Continue);
    QVERIFY2(!doc.isDirty() || restored,
             "the document is marked edited but its pages did not change");

    // A Save after the undo succeeds and writes the restored pages.
    QEXPECT_FAIL("delete page 2", "todo/2026-10-15-page-op-undo-after-save: Save fails after the undo", Continue);
    QVERIFY(doc.save());
    QEXPECT_FAIL("delete page 2", "todo/2026-10-15-page-op-undo-after-save: the file keeps the page deleted", Abort);
    QEXPECT_FAIL("move page 3 to the front", "todo/2026-10-15-page-op-undo-after-save: the file keeps the page moved", Abort);
    QCOMPARE(pageWidths(path), original);

    // ⌘⇧Z re-applies the operation, and the next Save writes it.
    QVERIFY(doc.redo());
    QCOMPARE(pageWidths(doc), afterOperation);
    QVERIFY(doc.isDirty());
    QVERIFY(doc.save());
    QCOMPARE(pageWidths(path), afterOperation);
}

void TestKnownBugsPdfSave::undoAfterSaveRevertsNewestFirst_data() {
    QTest::addColumn<bool>("markFirst");
    QTest::addColumn<bool>("saveAs");
    QTest::newRow("mark, rotate, save") << true << false;
    QTest::newRow("mark, rotate, save as") << true << true;
    QTest::newRow("rotate, mark, save") << false << false;
    QTest::newRow("rotate, mark, save as") << false << true;
}

// todo/2026-10-15-save-wipes-markup-undo: Saving a PDF wipes its markup undo history, so ⌘Z then undoes the wrong thing.
void TestKnownBugsPdfSave::undoAfterSaveRevertsNewestFirst() {
    QFETCH(bool, markFirst);
    QFETCH(bool, saveAs);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writePdf(dir.filePath("mark.pdf"), 1);
    PdfDocument doc(path);
    // Loads the document first: rotatePage() drops a rotate on a document not loaded yet.
    QVERIFY(doc.isValid());
    const QString neither = QStringLiteral("400 wide, 0 marks");
    const QString both = QStringLiteral("600 wide, 1 mark");
    // What is left once the newer of the two edits is undone.
    const QString older =
        markFirst ? QStringLiteral("400 wide, 1 mark") : QStringLiteral("600 wide, 0 marks");
    if (markFirst) {
        doc.annotations()->add(rectMark(0, QRectF(50, 50, 40, 30)));
        doc.rotatePage(0, 90);
    } else {
        doc.rotatePage(0, 90);
        doc.annotations()->add(rectMark(0, QRectF(50, 50, 40, 30)));
    }
    QCOMPARE(pageAndMarks(doc), both);
    QVERIFY(doc.save(saveAs ? dir.filePath(QStringLiteral("copy.pdf")) : QString()));
    QVERIFY(!doc.isDirty());
    QCOMPARE(pageAndMarks(doc), both);

    // ⌘Z reverts the newer edit, then the older one.
    QVERIFY(doc.undo());
    QEXPECT_FAIL("rotate, mark, save",
                 "todo/2026-10-15-save-wipes-markup-undo: ⌘Z after Save skips the newer mark and reverts the rotate", Abort);
    QEXPECT_FAIL("rotate, mark, save as",
                 "todo/2026-10-15-save-wipes-markup-undo: ⌘Z after Save As skips the newer mark and reverts the rotate", Abort);
    QCOMPARE(pageAndMarks(doc), older);
    QVERIFY(doc.isDirty());
    const bool undid = doc.undo();
    QEXPECT_FAIL("mark, rotate, save", "todo/2026-10-15-save-wipes-markup-undo: Save clears the markup undo history", Abort);
    QEXPECT_FAIL("mark, rotate, save as", "todo/2026-10-15-save-wipes-markup-undo: Save As clears the markup undo history", Abort);
    QCOMPARE(pageAndMarks(doc), neither);
    QVERIFY(undid);
    QVERIFY(doc.isDirty());
    QVERIFY(!doc.canUndo());

    // ⌘⇧Z re-applies the older edit, then the newer one.
    QVERIFY(doc.redo());
    QCOMPARE(pageAndMarks(doc), older);
    QVERIFY(doc.redo());
    QCOMPARE(pageAndMarks(doc), both);
    QVERIFY(!doc.canRedo());
}

QTEST_MAIN(TestKnownBugsPdfSave)
#include "test_known_bugs_pdf_save.moc"
