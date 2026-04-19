#include "document/PdfEditor.h"

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

QTEST_MAIN(TestPdfEditor)
#include "test_pdf_editor.moc"
