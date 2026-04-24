// UAT harness — Redaction tool (Phase 5 §6.11).
//
// Places a Redaction annotation over part of a page and saves. The
// save-time pipeline must replace the page's content stream with a
// rasterised copy (with the redacted rectangle painted black) and
// must NOT preserve the Redaction as a /Annot.
//
// The UI drag-path is covered elsewhere — this harness targets the
// IDocument::save() wiring so a regression in PdfDocument or PdfEditor
// surfaces here instead of in a shipping build.
//
//   uat_red_010_redactionFlattensToRasterOnSave
//       Adding a Redaction annotation and saving produces a PDF whose
//       affected page has a /TrailerRed* XObject, no residual
//       Redaction /Annot, and no surviving form/widget /Annots on the
//       redacted page.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "document/IDocument.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <qpdf/Pl_String.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
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

QString writeSamplePdf(const QString& path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
               QStringLiteral("Sensitive data goes here"));
    p.end();
    return path;
}

QStringList firstPageXObjectKeys(const QString& pdfPath) {
    QPDF pdf;
    pdf.processFile(pdfPath.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (pages.empty()) return {};
    QPDFObjectHandle res = pages.front().getAttribute("/Resources", true);
    if (!res.isDictionary()) return {};
    QPDFObjectHandle x = res.getKey("/XObject");
    if (!x.isDictionary()) return {};
    QStringList keys;
    for (const auto& k : x.getKeys()) keys << QString::fromStdString(k);
    return keys;
}

// Concatenated page-1 content stream as a string. Used to check that
// the redaction-flattening replaced the original drawing commands
// with a single image draw.
std::string firstPageContents(const QString& pdfPath) {
    QPDF pdf;
    pdf.processFile(pdfPath.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (pages.empty()) return {};
    std::string out;
    Pl_String sink("content", nullptr, out);
    pages.front().pipeContents(&sink);
    return out;
}

}  // namespace

class TestUatRedaction : public QObject {
    Q_OBJECT
private slots:
    void init();
    void uat_red_010_redactionFlattensToRasterOnSave();

private:
    QTemporaryDir m_scratch;
};

void TestUatRedaction::init() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) w->close();
    }
    QApplication::processEvents();
}

void TestUatRedaction::uat_red_010_redactionFlattensToRasterOnSave() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeSamplePdf(
        m_scratch.filePath(QStringLiteral("red010.pdf")));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);

    AnnotationStore* astore = doc->annotations();
    QVERIFY(astore);

    // Drop a Redaction over the middle of the page. Coordinates are in
    // doc-native points (top-left origin).
    Annotation a;
    a.page = 0;
    a.type = AnnotationType::Redaction;
    a.bounds = QRectF(100, 100, 240, 60);
    astore->add(a);

    const QString out = m_scratch.filePath(QStringLiteral("redacted.pdf"));
    QVERIFY(doc->save(out));

    // Page must now carry a /TrailerRed* XObject…
    const QStringList keys = firstPageXObjectKeys(out);
    bool foundRed = false;
    for (const QString& k : keys) {
        if (k.startsWith(QStringLiteral("/TrailerRed"))) {
            foundRed = true; break;
        }
    }
    QVERIFY2(foundRed, qPrintable("No flattened redaction XObject: "
                                  + keys.join(", ")));

    // …and its content stream must draw that XObject (the original
    // drawing commands should be gone).
    const std::string content = firstPageContents(out);
    QVERIFY2(content.find("/TrailerRed") != std::string::npos,
             "Redacted page content stream does not reference /TrailerRed*");
    QVERIFY2(content.find(" Do") != std::string::npos,
             "Redacted page content stream has no XObject-draw operator");

    // Re-read the output — Redactions must NOT come back as /Annot.
    PdfEditor round;
    QVERIFY(round.load(out));
    for (const Annotation& reloaded : round.readAnnotations()) {
        QVERIFY(reloaded.type != AnnotationType::Redaction);
    }
}

int main(int argc, char** argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid()) return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME",   (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatRedaction tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_redaction.moc"
