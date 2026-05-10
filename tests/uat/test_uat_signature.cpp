// UAT harness — Sign tool (Phase 5 §6.4.3).
//
// Stamps a saved signature onto a freshly-loaded PDF, saves, and
// re-reads the file to confirm the PNG was flattened into the page
// content stream (not left dangling as a /Annot reference).
//
// We drive the AnnotationStore + PdfAdapter directly rather than
// simulating mouse drags on the overlay — the on-screen placement
// path is covered by test_pdf_editor's flatten test; this harness
// focuses on the end-to-end IDocument::save() wiring.
//
//   uat_sig_010_signatureRoundTripsAsFlattenedImage
//       Adding a Signature annotation and saving produces a PDF whose
//       first page has a /TrailerSig* image XObject and no residual
//       Signature /Annot.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "document/IDocument.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "signatures/SignatureStore.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

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

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QString writeSampleSignedPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, QStringLiteral("Sign here"));
    p.end();
    return path;
}

QString writeSamplePng(const QString &path) {
    QImage img(40, 20, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0, 0));
    QPainter p(&img);
    p.setPen(QColor(20, 20, 20, 230));
    p.drawLine(0, 10, 40, 10);
    p.end();
    img.save(path, "PNG");
    return path;
}

QStringList firstPageXObjectKeys(const QString &pdfPath) {
    QPDF pdf;
    pdf.processFile(pdfPath.toLocal8Bit().constData());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (pages.empty())
        return {};
    QPDFObjectHandle res = pages.front().getAttribute("/Resources", true);
    if (!res.isDictionary())
        return {};
    QPDFObjectHandle x = res.getKey("/XObject");
    if (!x.isDictionary())
        return {};
    QStringList keys;
    for (const auto &k : x.getKeys())
        keys << QString::fromStdString(k);
    return keys;
}

} // namespace

class TestUatSignature : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_sig_010_signatureRoundTripsAsFlattenedImage();

  private:
    QTemporaryDir m_scratch;
};

void TestUatSignature::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatSignature::uat_sig_010_signatureRoundTripsAsFlattenedImage() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeSampleSignedPdf(m_scratch.filePath(QStringLiteral("sig010.pdf")));
    const QString pngPath = writeSamplePng(m_scratch.filePath(QStringLiteral("ink.png")));

    // Seed the store via the public API so filesystem layout matches
    // what the Sign Here flow would use at runtime.
    SignatureStore store;
    QImage ink(pngPath);
    QVERIFY(!ink.isNull());
    const Signature sig = store.add(ink, QStringLiteral("UAT"));
    QVERIFY(!sig.id.isEmpty());

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    AnnotationStore *astore = doc->annotations();
    QVERIFY(astore);

    // Drop a Signature annotation directly into the store — bypasses
    // the drag flow but exercises the same save-path.
    Annotation a;
    a.page = 0;
    a.type = AnnotationType::Signature;
    a.bounds = QRectF(120, 400, 200, 60);
    a.imagePath = sig.pngPath;
    astore->add(a);

    const QString out = m_scratch.filePath(QStringLiteral("signed.pdf"));
    QVERIFY(doc->save(out));

    // Confirm the PNG landed as a /TrailerSig* XObject on page 1.
    const QStringList keys = firstPageXObjectKeys(out);
    bool found = false;
    for (const QString &k : keys) {
        if (k.startsWith(QStringLiteral("/TrailerSig"))) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, qPrintable("No flattened signature XObject: " + keys.join(", ")));

    // …and that it did NOT come back as a /Annot on re-read (that would
    // mean writeAnnotations forgot to skip the Signature type).
    PdfEditor round;
    QVERIFY(round.load(out));
    for (const Annotation &reloaded : round.readAnnotations()) {
        QVERIFY(reloaded.type != AnnotationType::Signature);
    }
}

int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer/signatures");

    Application app(argc, argv);
    TestUatSignature tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_signature.moc"
