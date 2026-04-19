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

QTEST_MAIN(TestPdfEditor)
#include "test_pdf_editor.moc"
