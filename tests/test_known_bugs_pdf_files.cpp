// Known bugs in how Trailer reads, writes and renders PDF files: redaction, password-protected
// files, the file identity of an in-place Save, print, and annotation types Trailer does not
// manage. Fixtures are written at run time; pages are checked by counting coloured pixels and,
// for redaction, by extracting their text.
#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/IDocument.h"
#include "document/PdfAdapter.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QtTest/QtTest>

#include <algorithm>
#include <climits>
#include <memory>
#include <optional>
#include <string>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace trailer;

namespace {

bool isRed(QRgb c) {
    return qRed(c) > 170 && qGreen(c) < 110 && qBlue(c) < 110;
}
bool isBlue(QRgb c) {
    return qBlue(c) > 170 && qRed(c) < 110 && qGreen(c) < 110;
}
bool isInk(QRgb c) {
    return qRed(c) < 100 && qGreen(c) < 100 && qBlue(c) < 100;
}
bool isPaper(QRgb c) {
    return (c & 0xffffff) == 0xffffff;
}

// Pixels matching `pred` inside `region` (the whole image when empty); `bbox` receives their
// bounding box.
int countPixels(const QImage &image, bool (*pred)(QRgb), QRect region = {}, QRect *bbox = nullptr) {
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    region = region.isEmpty() ? img.rect() : region.intersected(img.rect());
    int n = 0;
    int minX = INT_MAX, minY = INT_MAX, maxX = -1, maxY = -1;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = region.left(); x <= region.right(); ++x) {
            if (!pred(line[x]))
                continue;
            ++n;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (bbox)
        *bbox = n ? QRect(QPoint(minX, minY), QPoint(maxX, maxY)) : QRect();
    return n;
}

// Page `page` of `path` rendered at 1 px per point of the displayed page, on white.
QImage renderPage(const QString &path, int page = 0, bool withAnnotations = false) {
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None || page >= pdf.pageCount())
        return {};
    QPdfDocumentRenderOptions options;
    if (withAnnotations)
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    const QImage raw = pdf.render(page, pdf.pagePointSize(page).toSize(), options);
    QImage out(raw.size(), QImage::Format_ARGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.drawImage(0, 0, raw);
    painter.end();
    return out;
}

// The text pdfium extracts from page `page` of `path`, without whitespace.
QString pageText(const QString &path, int page = 0) {
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None || page >= pdf.pageCount())
        return QStringLiteral("(no such page)");
    return pdf.getAllText(page).text().remove(QRegularExpression(QStringLiteral("\\s")));
}

// Whether `text` holds `word` read either way: on a page turned 180°, pdfium
// extracts the text back to front ("1174TERCES").
bool holdsWord(const QString &text, const QString &word) {
    QString reversed = text;
    std::reverse(reversed.begin(), reversed.end());
    return text.contains(word) || reversed.contains(word);
}

// `pages` A4 pages, each with a black "Page N" line near the top.
bool writeTextPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(72);
    QPainter painter(&writer);
    if (!painter.isActive())
        return false;
    QFont font;
    font.setPointSize(24);
    painter.setFont(font);
    painter.setPen(Qt::black);
    for (int i = 0; i < pages; ++i) {
        if (i > 0)
            writer.newPage();
        painter.drawText(QRect(40, 40, 480, 60), Qt::AlignLeft,
                         QStringLiteral("Page %1").arg(i + 1));
    }
    return painter.end();
}

// One A4 page: a blue heading and, lower down, red "SECRET 4711". Both stay inside a crop of
// 150 pt from the left and top edges.
bool writeSecretPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(72);
    QPainter painter(&writer);
    if (!painter.isActive())
        return false;
    QFont font;
    font.setPointSize(28);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(0, 0, 255));
    painter.drawText(QRect(180, 200, 400, 60), Qt::AlignLeft, QStringLiteral("Public heading"));
    painter.setPen(QColor(255, 0, 0));
    painter.drawText(QRect(300, 600, 240, 60), Qt::AlignLeft, QStringLiteral("SECRET 4711"));
    return painter.end();
}

// Copies `in` to `out`, giving page 0 a /Rotate of `rotate` and, when `inset` > 0, a /CropBox
// inset that many points from the left and top edges of its MediaBox.
bool writeWithPageGeometry(const QString &in, const QString &out, int rotate, double inset) {
    try {
        QPDF pdf;
        pdf.processFile(in.toLocal8Bit().constData());
        QPDFPageObjectHelper page = QPDFPageDocumentHelper(pdf).getAllPages().at(0);
        QPDFObjectHandle obj = page.getObjectHandle();
        if (rotate != 0)
            obj.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotate));
        if (inset > 0) {
            const auto media = page.getMediaBox().getArrayAsRectangle();
            obj.replaceKey("/CropBox",
                           QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(
                               media.llx + inset, media.lly, media.urx, media.ury - inset)));
        }
        const QByteArray name = out.toLocal8Bit();
        QPDFWriter writer(pdf, name.constData());
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// Copies `in` to `out` encrypted with AES-256 (R6). `restricted` forbids assembly, annotation,
// form filling and other modification.
bool writeEncrypted(const QString &in, const QString &out, const char *userPassword,
                    const char *ownerPassword, bool restricted) {
    try {
        QPDF pdf;
        pdf.processFile(in.toLocal8Bit().constData());
        const QByteArray name = out.toLocal8Bit();
        QPDFWriter writer(pdf, name.constData());
        writer.setR6EncryptionParameters(userPassword, ownerPassword, true, true, !restricted,
                                         !restricted, !restricted, !restricted, qpdf_r3p_full,
                                         true);
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// The rectangles of the annotations in writeUnmanagedAnnotationsPdf, in PDF points.
const QRectF kStampRect(100, 400, 200, 80);
const QRectF kPolygonRect(350, 100, 120, 120);

// One Letter page with black text and a /Stamp and/or a /Polygon annotation. Each annotation's
// /AP stream fills its rectangle red.
bool writeUnmanagedAnnotationsPdf(const QString &path, bool withStamp, bool withPolygon) {
    try {
        QPDF pdf;
        pdf.emptyPDF();
        auto appearance = [&pdf](const QRectF &r) {
            const std::string size =
                std::to_string(int(r.width())) + " " + std::to_string(int(r.height()));
            QPDFObjectHandle form =
                QPDFObjectHandle::newStream(&pdf, "1 0 0 rg 0 0 " + size + " re f\n");
            form.replaceDict(QPDFObjectHandle::parse(
                "<< /Type /XObject /Subtype /Form /BBox [0 0 " + size + "] >>"));
            QPDFObjectHandle ap = QPDFObjectHandle::newDictionary();
            ap.replaceKey("/N", form);
            return ap;
        };
        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        if (withStamp) {
            QPDFObjectHandle stamp = QPDFObjectHandle::parse(
                "<< /Type /Annot /Subtype /Stamp /Name /Approved /Rect [100 400 300 480] "
                "/T (Alice) /F 4 >>");
            stamp.replaceKey("/AP", appearance(kStampRect));
            annots.appendItem(pdf.makeIndirectObject(stamp));
        }
        if (withPolygon) {
            QPDFObjectHandle polygon = QPDFObjectHandle::parse(
                "<< /Type /Annot /Subtype /Polygon /Rect [350 100 470 220] "
                "/Vertices [350 100 470 100 470 220 350 220] /C [1 0 0] /IC [1 0 0] /F 4 >>");
            polygon.replaceKey("/AP", appearance(kPolygonRect));
            annots.appendItem(pdf.makeIndirectObject(polygon));
        }

        QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
        fonts.replaceKey("/F1", pdf.makeIndirectObject(QPDFObjectHandle::parse(
                                    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
        QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/Font", fonts);
        QPDFObjectHandle page =
            QPDFObjectHandle::parse("<< /Type /Page /MediaBox [0 0 612 792] >>");
        page.replaceKey("/Contents",
                        QPDFObjectHandle::newStream(
                            &pdf, "BT /F1 24 Tf 72 700 Td (Page with a stamp) Tj ET\n"));
        page.replaceKey("/Resources", resources);
        page.replaceKey("/Annots", annots);
        QPDFPageDocumentHelper(pdf).addPage(QPDFPageObjectHelper(pdf.makeIndirectObject(page)),
                                            false);
        const QByteArray name = path.toLocal8Bit();
        QPDFWriter writer(pdf, name.constData());
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// The /Subtype of each entry in page 0's /Annots. With `markAppearance`, "+AP" is appended to
// entries that carry an /AP dictionary.
QStringList annotationSubtypes(const QString &path, bool markAppearance = false) {
    try {
        QPDF pdf;
        pdf.processFile(path.toLocal8Bit().constData());
        QStringList out;
        QPDFObjectHandle annots =
            QPDFPageDocumentHelper(pdf).getAllPages().at(0).getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            return out;
        for (QPDFObjectHandle a : annots.aitems()) {
            if (!a.isDictionary())
                continue;
            QPDFObjectHandle subtype = a.getKey("/Subtype");
            QString s =
                subtype.isName() ? QString::fromStdString(subtype.getName()) : QStringLiteral("?");
            if (markAppearance && a.getKey("/AP").isDictionary())
                s += QStringLiteral("+AP");
            out << s;
        }
        return out;
    } catch (const std::exception &e) {
        return {QStringLiteral("error: %1").arg(QString::fromLocal8Bit(e.what()))};
    }
}

Annotation redRectangle(bool filled) {
    Annotation a;
    a.page = 0;
    a.type = AnnotationType::Rectangle;
    a.bounds = QRectF(100, 300, 80, 40);
    a.style.stroke = QColor(255, 0, 0);
    a.style.strokeWidth = 3;
    if (filled)
        a.style.fill = QColor(255, 0, 0);
    return a;
}

// Opens `path` the way the app does (PdfAdapter::open), answering its password prompt with
// `password`.
std::unique_ptr<IDocument> openPdf(const QString &path, const QString &password = {}) {
    PdfAdapter::setPasswordPrompt([password](const QString &, int) -> std::optional<QString> {
        if (password.isEmpty())
            return std::nullopt;
        return password;
    });
    const auto restore = qScopeGuard([] { PdfAdapter::setPasswordPrompt({}); });
    PdfAdapter adapter;
    return adapter.open(path);
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// Runs doc.print() and answers its print dialog: PDF output to `outPath`, A4, 72 dpi, no
// margins. Returns false if no print dialog appeared.
bool printToPdf(IDocument &doc, const QString &outPath) {
    QObject context;
    bool answered = false;
    QTimer poll;
    poll.setInterval(0);
    QObject::connect(&poll, &QTimer::timeout, &context, [&]() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QPrintDialog *>(w);
            if (!dialog || !dialog->isVisible())
                continue;
            QPrinter *printer = dialog->printer();
            printer->setOutputFormat(QPrinter::PdfFormat);
            printer->setOutputFileName(outPath);
            printer->setResolution(72);
            printer->setPageSize(QPageSize(QPageSize::A4));
            printer->setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
            answered = true;
            poll.stop();
            // done() rather than accept(): accept() copies the dialog's widgets back into the
            // printer and would undo the settings above.
            dialog->done(QDialog::Accepted);
            return;
        }
    });
    poll.start();
    doc.print(nullptr);
    poll.stop();
    return answered;
}
#endif

#ifndef Q_OS_WIN
// Permission bits of `path` in octal, e.g. "600".
QString fileMode(const QString &path) {
    struct stat st {};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return QStringLiteral("missing");
    return QString::number(st.st_mode & 07777, 8);
}
#endif

} // namespace

class TestKnownBugsPdfFiles : public QObject {
    Q_OBJECT
  private slots:
    void redactionCoversTextOnRotatedAndCroppedPages_data();
    void redactionCoversTextOnRotatedAndCroppedPages();

    void encryptedPdfKeepsPagesAfterPageOperation();
    void encryptedPdfSavesPageOperation();
    void encryptedPdfWritesRecoverySnapshot();
    void ownerPasswordOnlyPdfSavesPageOperation();

    void saveThroughSymlinkUpdatesTargetAndKeepsLink();
    void saveKeepsFileMode_data();
    void saveKeepsFileMode();

    void printIncludesAnnotations_data();
    void printIncludesAnnotations();

    void viewShowsUnmanagedAnnotationTypes_data();
    void viewShowsUnmanagedAnnotationTypes();
    void saveKeepsUnmanagedAnnotationTypes();
};

void TestKnownBugsPdfFiles::redactionCoversTextOnRotatedAndCroppedPages_data() {
    QTest::addColumn<int>("rotate");
    QTest::addColumn<double>("cropInset");
    QTest::newRow("unrotated") << 0 << 0.0;
    QTest::newRow("rotate90") << 90 << 0.0;
    QTest::newRow("rotate180") << 180 << 0.0;
    QTest::newRow("rotate270") << 270 << 0.0;
    QTest::newRow("cropped150") << 0 << 150.0;
}

// T-90: Redaction leaves text visible on rotated and cropped pages
void TestKnownBugsPdfFiles::redactionCoversTextOnRotatedAndCroppedPages() {
    QFETCH(int, rotate);
    QFETCH(double, cropInset);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("source.pdf"));
    const QString path = dir.filePath(QStringLiteral("secret.pdf"));
    QVERIFY(writeSecretPdf(source));
    QVERIFY(writeWithPageGeometry(source, path, rotate, cropInset));

    // Where the secret and the heading are on the displayed page.
    const QImage before = renderPage(path);
    QRect secret;
    QVERIFY(countPixels(before, isRed, {}, &secret) > 500);
    QRect heading;
    const int headingBefore = countPixels(before, isBlue, {}, &heading);
    QVERIFY(headingBefore > 500);
    QVERIFY(!heading.intersects(secret));
    QVERIFY(holdsWord(pageText(path), QStringLiteral("SECRET4711")));

    // A redaction over the secret as displayed, a few points larger all round.
    PdfDocument doc(path);
    QCOMPARE(doc.pageCount(), 1);
    Annotation redaction;
    redaction.page = 0;
    redaction.type = AnnotationType::Redaction;
    redaction.bounds = QRectF(secret).adjusted(-6, -6, 6, 6);
    doc.annotations()->add(redaction);
    QVERIFY(doc.save());

    const QImage after = renderPage(path);
    QCOMPARE(after.size(), before.size());
    // Removed from the file, not just painted over: no text extraction finds it.
    const QString text = pageText(path);
    QVERIFY2(!holdsWord(text, QStringLiteral("SECRET")) && !holdsWord(text, QStringLiteral("4711")),
             qPrintable(QStringLiteral("the saved page's text: %1").arg(text)));
    QEXPECT_FAIL("rotate90",
                 "T-90: the rotated page is stretched to the MediaBox; the box misses the text",
                 Abort);
    QEXPECT_FAIL("rotate270",
                 "T-90: the rotated page is stretched to the MediaBox; the box misses the text",
                 Abort);
    QEXPECT_FAIL("cropped150",
                 "T-90: the cropped page is stretched to the MediaBox; the box misses the text",
                 Abort);
    QCOMPARE(countPixels(after, isRed), 0);

    // The page reads as it did before: the heading is still where it was.
    const int headingAfter = countPixels(after, isBlue, heading);
    QEXPECT_FAIL("rotate180",
                 "T-90: /Rotate turns the already rotated raster again; the page flips upright",
                 Abort);
    QVERIFY2(headingAfter >= headingBefore / 2,
             qPrintable(QStringLiteral("heading pixels in place: %1 before, %2 after")
                            .arg(headingBefore)
                            .arg(headingAfter)));
}

// T-91: Password-protected PDFs go blank after any page operation, and Save misreports
void TestKnownBugsPdfFiles::encryptedPdfKeepsPagesAfterPageOperation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
    const QString path = dir.filePath(QStringLiteral("locked.pdf"));
    QVERIFY(writeTextPdf(plain, 3));
    QVERIFY(writeEncrypted(plain, path, "user", "owner", false));

    const auto opened = openPdf(path, QStringLiteral("user"));
    auto *doc = dynamic_cast<PdfDocument *>(opened.get());
    QVERIFY(doc);
    QVERIFY(!doc->needsPassword());
    QCOMPARE(doc->pageCount(), 3);

    doc->rotatePage(0, 90);
    QEXPECT_FAIL("", "T-91: the view reloads the edited copy without the password", Continue);
    QCOMPARE(doc->pageCount(), 3);
    QEXPECT_FAIL("", "T-91: the failed reload leaves the document marked clean", Abort);
    QVERIFY(doc->isDirty());
}

// T-91: Password-protected PDFs go blank after any page operation, and Save misreports
void TestKnownBugsPdfFiles::encryptedPdfSavesPageOperation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
    const QString path = dir.filePath(QStringLiteral("locked.pdf"));
    QVERIFY(writeTextPdf(plain, 3));
    QVERIFY(writeEncrypted(plain, path, "user", "owner", false));

    {
        const auto opened = openPdf(path, QStringLiteral("user"));
        auto *doc = dynamic_cast<PdfDocument *>(opened.get());
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 3);
        doc->rotatePage(0, 90);
        const bool saved = doc->save();
        QEXPECT_FAIL("", "T-91: Save reloads the saved file without the password", Continue);
        QVERIFY(saved);
    }

    // Reopened with the password, the file has three pages and the first is rotated.
    const auto reopened = openPdf(path, QStringLiteral("user"));
    auto *doc = dynamic_cast<PdfDocument *>(reopened.get());
    QVERIFY(doc);
    QVERIFY(!doc->needsPassword());
    QCOMPARE(doc->pageCount(), 3);
    const QSizeF first = doc->pageSizeHint(0);
    QVERIFY2(
        first.width() > first.height(),
        qPrintable(QStringLiteral("page 1 is %1 x %2 pt").arg(first.width()).arg(first.height())));
}

// T-91: Password-protected PDFs go blank after any page operation, and Save misreports
void TestKnownBugsPdfFiles::encryptedPdfWritesRecoverySnapshot() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
    const QString path = dir.filePath(QStringLiteral("locked.pdf"));
    QVERIFY(writeTextPdf(plain, 3));
    QVERIFY(writeEncrypted(plain, path, "user", "owner", false));

    const auto opened = openPdf(path, QStringLiteral("user"));
    auto *doc = dynamic_cast<PdfDocument *>(opened.get());
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 3);
    doc->annotations()->add(redRectangle(false));
    QVERIFY(doc->isDirty());

    const QString sidecar = dir.filePath(QStringLiteral("locked.recovery.pdf"));
    const bool written = doc->writeRecoverySnapshot(sidecar);
    QEXPECT_FAIL("", "T-91: the snapshot copy is reopened without the password", Abort);
    QVERIFY(written);
    QVERIFY(QFileInfo(sidecar).size() > 0);
}

// T-91: Password-protected PDFs go blank after any page operation, and Save misreports
void TestKnownBugsPdfFiles::ownerPasswordOnlyPdfSavesPageOperation() {
    // Control case: restricted permissions and no open password. Passes today.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
    const QString path = dir.filePath(QStringLiteral("restricted.pdf"));
    QVERIFY(writeTextPdf(plain, 3));
    QVERIFY(writeEncrypted(plain, path, "", "owner", true));

    {
        const auto opened = openPdf(path);
        auto *doc = dynamic_cast<PdfDocument *>(opened.get());
        QVERIFY(doc);
        QVERIFY(!doc->needsPassword());
        QCOMPARE(doc->pageCount(), 3);
        doc->rotatePage(0, 90);
        QCOMPARE(doc->pageCount(), 3);
        QVERIFY(doc->isDirty());
        QVERIFY(doc->save());
        QCOMPARE(doc->pageCount(), 3);
        QVERIFY(!doc->isDirty());
    }

    const auto reopened = openPdf(path);
    auto *doc = dynamic_cast<PdfDocument *>(reopened.get());
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 3);
    const QSizeF first = doc->pageSizeHint(0);
    QVERIFY(first.width() > first.height());
}

// T-92: In-place Save deletes the file before replacing it, breaks symlinks, and resets permissions
void TestKnownBugsPdfFiles::saveThroughSymlinkUpdatesTargetAndKeepsLink() {
#ifdef Q_OS_WIN
    QSKIP("Needs POSIX symlinks");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString real = dir.filePath(QStringLiteral("real.pdf"));
    const QString link = dir.filePath(QStringLiteral("link.pdf"));
    QVERIFY(writeTextPdf(real, 1));
    QVERIFY(QFile::link(real, link));
    QVERIFY(QFileInfo(link).isSymLink());

    PdfDocument doc(link);
    QCOMPARE(doc.pageCount(), 1);
    doc.annotations()->add(redRectangle(false));
    QVERIFY(doc.save());

    QEXPECT_FAIL("", "T-92: Save replaces the symlink with a regular file", Continue);
    QVERIFY(QFileInfo(link).isSymLink());
    const QStringList target = annotationSubtypes(real);
    QEXPECT_FAIL("", "T-92: Save leaves the symlink's target unchanged", Abort);
    QVERIFY2(target.contains(QStringLiteral("/Square")),
             qPrintable(QStringLiteral("target annotations: [%1]").arg(target.join(u','))));
#endif
}

void TestKnownBugsPdfFiles::saveKeepsFileMode_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("0600") << 0600;
    QTest::newRow("0664") << 0664;
}

// T-92: In-place Save deletes the file before replacing it, breaks symlinks, and resets permissions
void TestKnownBugsPdfFiles::saveKeepsFileMode() {
#ifdef Q_OS_WIN
    QSKIP("Needs POSIX file modes");
#else
    QFETCH(int, mode);
    // A fixed umask, so the result does not depend on the caller's.
    const mode_t previousMask = ::umask(022);
    const auto restoreMask = qScopeGuard([previousMask] { ::umask(previousMask); });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("private.pdf"));
    QVERIFY(writeTextPdf(path, 1));
    QCOMPARE(::chmod(QFile::encodeName(path).constData(), mode_t(mode)), 0);
    const QString expected = QString::number(mode, 8);
    QCOMPARE(fileMode(path), expected);

    PdfDocument doc(path);
    QCOMPARE(doc.pageCount(), 1);
    doc.annotations()->add(redRectangle(false));
    QVERIFY(doc.save());
    QVERIFY(annotationSubtypes(path).contains(QStringLiteral("/Square")));

    QEXPECT_FAIL("0600", "T-92: Save recreates the file with the default mode", Abort);
    QEXPECT_FAIL("0664", "T-92: Save recreates the file with the default mode", Abort);
    QCOMPARE(fileMode(path), expected);
#endif
}

void TestKnownBugsPdfFiles::printIncludesAnnotations_data() {
    QTest::addColumn<bool>("saveFirst");
    QTest::newRow("saved") << true;
    QTest::newRow("unsaved") << false;
}

// T-95: Print leaves out annotations
void TestKnownBugsPdfFiles::printIncludesAnnotations() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QSKIP("The print dialog is native here and cannot be answered from a test");
#else
    QFETCH(bool, saveFirst);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("marked.pdf"));
    QVERIFY(writeTextPdf(path, 1));

    PdfDocument doc(path);
    QCOMPARE(doc.pageCount(), 1);
    const Annotation mark = redRectangle(true);
    doc.annotations()->add(mark);
    if (saveFirst) {
        QVERIFY(doc.save());
        QVERIFY(!doc.isDirty());
        // The saved mark is in the file and draws red.
        QVERIFY(countPixels(renderPage(path, 0, true), isRed) > 1000);
    }

    // Printed at 72 dpi on A4 without margins, 1 px of the print is 1 pt of the page.
    const QString printed = dir.filePath(QStringLiteral("printed.pdf"));
    QVERIFY(printToPdf(doc, printed));
    const QImage page = renderPage(printed);
    QVERIFY(!page.isNull());
    QVERIFY(countPixels(page, isInk) > 100); // the page's own text
    const int red = countPixels(page, isRed);
    const double area = mark.bounds.width() * mark.bounds.height();
    QEXPECT_FAIL("saved", "T-95: print renders pages without their annotations", Abort);
    QEXPECT_FAIL("unsaved", "T-95: print leaves out marks that are not saved yet", Abort);
    QVERIFY2(
        red >= area / 2,
        qPrintable(QStringLiteral("%1 red pixels printed; the mark covers %2").arg(red).arg(area)));
#endif
}

void TestKnownBugsPdfFiles::viewShowsUnmanagedAnnotationTypes_data() {
    QTest::addColumn<bool>("stamp");
    QTest::addColumn<QRectF>("rect");
    QTest::newRow("stamp") << true << kStampRect;
    QTest::newRow("polygon") << false << kPolygonRect;
}

// T-94: Annotation types Trailer doesn't manage are invisible
void TestKnownBugsPdfFiles::viewShowsUnmanagedAnnotationTypes() {
    QFETCH(bool, stamp);
    QFETCH(QRectF, rect);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("stamped.pdf"));
    QVERIFY(writeUnmanagedAnnotationsPdf(path, stamp, !stamp));
    // The appearance stream fills the rectangle red when a renderer draws annotations.
    const double area = rect.width() * rect.height();
    QVERIFY(countPixels(renderPage(path, 0, true), isRed) >= area * 0.9);

    PdfDocument doc(path);
    QCOMPARE(doc.pageCount(), 1);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view);
    view->resize(1000, 1200); // shows the whole page at 100%
    view->show();
    QVERIFY(QTest::qWaitForWindowExposed(view.get()));

    // Wait until the page itself is drawn: its black text shows in the grab.
    QImage grab;
    const auto pageDrawn = [&]() {
        grab = view->grab().toImage();
        return countPixels(grab, isInk) > 50;
    };
    QTRY_VERIFY_WITH_TIMEOUT(pageDrawn(), 10000);
    // The page's white area gives the scale it is drawn at.
    QRect page;
    QVERIFY(countPixels(grab, isPaper, {}, &page) > 10000);
    const double drawnArea = area * (page.width() / 612.0) * (page.width() / 612.0);

    const int red = countPixels(grab, isRed);
    QEXPECT_FAIL("stamp", "T-94: the view renders pages without annotation appearances", Abort);
    QEXPECT_FAIL("polygon", "T-94: the view renders pages without annotation appearances", Abort);
    QVERIFY2(red >= drawnArea / 2,
             qPrintable(QStringLiteral("%1 red pixels in the view; the appearance covers %2")
                            .arg(red)
                            .arg(drawnArea)));
}

// T-94: Annotation types Trailer doesn't manage are invisible
void TestKnownBugsPdfFiles::saveKeepsUnmanagedAnnotationTypes() {
    // Save keeps them in the file. Passes today.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("stamped.pdf"));
    QVERIFY(writeUnmanagedAnnotationsPdf(path, true, true));

    PdfDocument doc(path);
    QCOMPARE(doc.pageCount(), 1);
    doc.annotations()->add(redRectangle(false));
    QVERIFY(doc.save());

    const QStringList onDisk = annotationSubtypes(path, true);
    const QString list = onDisk.join(u',');
    QVERIFY2(onDisk.contains(QStringLiteral("/Stamp+AP")), qPrintable(list));
    QVERIFY2(onDisk.contains(QStringLiteral("/Polygon+AP")), qPrintable(list));
    QVERIFY2(onDisk.contains(QStringLiteral("/Square+AP")), qPrintable(list));
}

QTEST_MAIN(TestKnownBugsPdfFiles)
#include "test_known_bugs_pdf_files.moc"
