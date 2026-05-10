#include "document/IDocument.h"
#include "ui/ThumbnailModel.h"

#include <QImage>
#include <QtTest/QtTest>
#include <QWidget>

using namespace trailer;

namespace {

class TransparentThumbDoc final : public IDocument {
  public:
    QString displayName() const override { return QStringLiteral("t"); }
    QString filePath() const override { return QStringLiteral("/tmp/x"); }
    QWidget *createView(QWidget *parent) override {
        Q_UNUSED(parent);
        return nullptr;
    }
    bool supportsThumbnails() const override { return true; }
    int pageCount() const override { return 1; }
    QImage renderThumbnail(int /*pageIndex*/, QSize targetSize) override {
        QImage img(targetSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        return img;
    }
};

} // namespace

class TestThumbnailModel : public QObject {
    Q_OBJECT

  private slots:
    void transparentRenderGetsPaperWhiteCenterPixel();
};

void TestThumbnailModel::transparentRenderGetsPaperWhiteCenterPixel() {
    TransparentThumbDoc doc;
    ThumbnailModel model;
    model.setDocument(&doc);
    model.setThumbnailSize(QSize(32, 32));

    const QModelIndex idx = model.index(0, 0);
    QVERIFY(idx.isValid());
    const QPixmap pm = model.data(idx, Qt::DecorationRole).value<QPixmap>();
    QVERIFY(!pm.isNull());

    const QImage out = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(out.width() > 4 && out.height() > 4);
    const int x = out.width() / 2;
    const int y = out.height() / 2;
    const QRgb c = out.pixel(x, y);
    QCOMPARE(qRed(c), 255);
    QCOMPARE(qGreen(c), 255);
    QCOMPARE(qBlue(c), 255);
}

QTEST_MAIN(TestThumbnailModel)
#include "test_thumbnail_model.moc"
