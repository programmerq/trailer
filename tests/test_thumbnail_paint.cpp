// Unit test -- device-pixel-ratio-correct thumbnail scaling (ThumbnailPaint.h).
//
// Regression pin for the Retina sidebar-slack bug: the page-thumbnail
// delegate scales a dpr-stamped pixmap to a LOGICAL column width. Before
// the fix it called QPixmap::scaledToWidth(availW) directly, which on a
// dpr=2 pixmap paints at availW/2 logical width (small thumbnail, tall
// empty row, low page-number badge) -- reproduced by the owner on macOS
// but invisible on dpr=1 CI. This injects synthetic dpr=2/3 pixmaps so it
// exercises the HiDPI path deterministically on any display.

#include "ui/ThumbnailPaint.h"

#include <QImage>
#include <QPixmap>
#include <QtTest/QtTest>

#include <cmath>

using namespace trailer;

namespace {
// Page aspects (width/height) exercised by the fixtures below.
constexpr double kA4Aspect = 0.707;      // portrait A4-ish
constexpr double kPanoramaAspect = 2.5;  // extreme landscape panorama

// A synthetic thumbnail pixmap as ThumbnailModel::data() produces it:
// raw pixels = logical * dpr, with dpr stamped. pageAspect = w/h.
QPixmap makeThumb(int logicalW, double pageAspect, qreal dpr) {
    const int logicalH = int(std::lround(logicalW / pageAspect));
    QImage img(int(std::ceil(logicalW * dpr)), int(std::ceil(logicalH * dpr)),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    img.setDevicePixelRatio(dpr);
    return QPixmap::fromImage(img);
}
} // namespace

class TestThumbnailPaint : public QObject {
    Q_OBJECT
  private slots:
    void logicalWidthMatchesColumn_data();
    void logicalWidthMatchesColumn();
    void rescalesWhenSourceWidthDiffers();
    void aspectPreservedAcrossDpr();
    void nullAndDegenerateInputs();
};

void TestThumbnailPaint::logicalWidthMatchesColumn_data() {
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<int>("availW");
    QTest::newRow("dpr1 w200") << qreal(1.0) << 200;
    QTest::newRow("dpr2 w200") << qreal(2.0) << 200; // Retina -- the bug case
    QTest::newRow("dpr3 w200") << qreal(3.0) << 200;
    QTest::newRow("dpr2 w168") << qreal(2.0) << 168; // narrow sidebar
    QTest::newRow("dpr2 w328") << qreal(2.0) << 328; // wide sidebar
    // Fractional scaling factors (Windows / GNOME). 167 exercises the
    // rounding path; the impl is provably exact for dpr>=1, so these PASS.
    QTest::newRow("dpr1.25 w167") << qreal(1.25) << 167;
    QTest::newRow("dpr1.5 w167") << qreal(1.5) << 167;
    QTest::newRow("dpr2.75 w167") << qreal(2.75) << 167;
}

void TestThumbnailPaint::logicalWidthMatchesColumn() {
    QFETCH(qreal, dpr);
    QFETCH(int, availW);
    const QPixmap src = makeThumb(availW, kA4Aspect, dpr); // portrait A4-ish
    const QPixmap scaled = scaleToLogicalWidth(src, availW);
    QVERIFY(!scaled.isNull());
    const QSize logical = logicalSize(scaled);
    // The drawn thumbnail must fill the column in LOGICAL pixels for every
    // devicePixelRatio -- the assertion the pre-fix code fails at dpr>1
    // (it produced availW/dpr).
    QVERIFY2(std::abs(logical.width() - availW) <= 1,
             qPrintable(QStringLiteral("dpr=%1 availW=%2: drawn logical width "
                                       "%3, expected %2")
                            .arg(dpr).arg(availW).arg(logical.width())));
    // dpr retained so the pixmap stays crisp (raw pixels ~= availW*dpr).
    QCOMPARE(scaled.devicePixelRatio(), dpr);
    QVERIFY2(std::abs(scaled.width() - int(std::lround(availW * dpr))) <= 1,
             "raw device width must be availW*dpr for HiDPI crispness");
}

void TestThumbnailPaint::rescalesWhenSourceWidthDiffers() {
    // The paint must scale to the CURRENT column width even when the source
    // pixmap's logical width differs from it -- the clamp / hysteresis /
    // stale-render path, where a cached pixmap rendered at one width is
    // painted into a column of another. Source logical width 240; column 167.
    const QPixmap src = makeThumb(240, kA4Aspect, 2.0);
    const QPixmap scaled = scaleToLogicalWidth(src, 167);
    QVERIFY2(std::abs(logicalSize(scaled).width() - 167) <= 1,
             "must rescale to the current column width, not the source width");
}

void TestThumbnailPaint::aspectPreservedAcrossDpr() {
    // A landscape page (aspect 2.5) at availW must have logical height
    // ~= availW/2.5 regardless of dpr -- the sidebar row-height oracle
    // depends on this holding on Retina.
    const int availW = 200;
    for (qreal dpr : {qreal(1.0), qreal(2.0), qreal(3.0)}) {
        const QPixmap scaled =
            scaleToLogicalWidth(makeThumb(availW, kPanoramaAspect, dpr), availW);
        const QSize logical = logicalSize(scaled);
        const int expH = int(std::lround(availW / kPanoramaAspect));
        QVERIFY2(std::abs(logical.height() - expH) <= 2,
                 qPrintable(QStringLiteral("landscape dpr=%1: logical height %2, "
                                           "expected ~%3")
                                .arg(dpr).arg(logical.height()).arg(expH)));
    }
    // Also sweep a PORTRAIT aspect (A4) so the tall-row case is pinned on
    // Retina too: logical height ~= round(availW/kA4Aspect).
    for (qreal dpr : {qreal(1.0), qreal(2.0), qreal(3.0)}) {
        const QPixmap scaled =
            scaleToLogicalWidth(makeThumb(availW, kA4Aspect, dpr), availW);
        const QSize logical = logicalSize(scaled);
        const int expH = int(std::lround(availW / kA4Aspect));
        QVERIFY2(std::abs(logical.height() - expH) <= 2,
                 qPrintable(QStringLiteral("portrait dpr=%1: logical height %2, "
                                           "expected ~%3")
                                .arg(dpr).arg(logical.height()).arg(expH)));
    }
}

void TestThumbnailPaint::nullAndDegenerateInputs() {
    QVERIFY(scaleToLogicalWidth(QPixmap(), 200).isNull());
    QVERIFY(scaleToLogicalWidth(makeThumb(100, kA4Aspect, 2.0), 0).isNull());
    QVERIFY(scaleToLogicalWidth(makeThumb(100, kA4Aspect, 2.0), -5).isNull());
}

QTEST_MAIN(TestThumbnailPaint)
#include "test_thumbnail_paint.moc"
