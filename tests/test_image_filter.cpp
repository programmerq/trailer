#include "filters/ImageFilter.h"

#include <QImage>
#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

// Per-channel comparison helper: QImage::pixel() returns QRgb; we want
// to compare R/G/B/A separately because most built-ins only touch one
// plane and the qRgba packing makes diffs unreadable on failure.
namespace {

QImage makeSolid(QRgb colour, int w = 4, int h = 4) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(colour);
    return img;
}

int r(const QImage& img, int x, int y) { return qRed(img.pixel(x, y)); }
int g(const QImage& img, int x, int y) { return qGreen(img.pixel(x, y)); }
int b(const QImage& img, int x, int y) { return qBlue(img.pixel(x, y)); }
int a(const QImage& img, int x, int y) { return qAlpha(img.pixel(x, y)); }

}  // namespace

class TestImageFilter : public QObject {
    Q_OBJECT
private slots:
    void idRoundTripIsStable();
    void idRejectsUnknownToNone();
    void allFiltersListsEveryEnumValue();
    void noneIsIdentity();
    void nullImagePassesThrough();
    void greyscaleProducesEqualChannels();
    void greyscalePreservesAlpha();
    void blackAndWhiteSnapsToZeroOrFullWhite();
    void sepiaShiftsNeutralGreyWarm();
    void lightenAddsBoostAndClampsAtWhite();
    void blueTonePushesBlueAboveOthers();
    void greyToneIsGentlerThanGreyscale();
    void applyByIdMatchesApplyByEnum();
};

void TestImageFilter::idRoundTripIsStable() {
    for (auto f : allFilters()) {
        QCOMPARE(filterFromId(filterId(f)), f);
    }
}

void TestImageFilter::idRejectsUnknownToNone() {
    QCOMPARE(filterFromId(QStringLiteral("")), ImageFilter::None);
    QCOMPARE(filterFromId(QStringLiteral("nonsense")), ImageFilter::None);
    // Ids are case-sensitive — this documents the contract rather than
    // locks it: if we ever want case-insensitive matching we'd update
    // this test deliberately.
    QCOMPARE(filterFromId(QStringLiteral("Sepia")), ImageFilter::None);
}

void TestImageFilter::allFiltersListsEveryEnumValue() {
    const auto list = allFilters();
    // Seven entries — None + six built-ins. If this ever grows the
    // test should grow with it so nothing silently drops out of the UI.
    QCOMPARE(list.size(), 7);
    QVERIFY(list.contains(ImageFilter::None));
    QVERIFY(list.contains(ImageFilter::BlackAndWhite));
    QVERIFY(list.contains(ImageFilter::Greyscale));
    QVERIFY(list.contains(ImageFilter::Sepia));
    QVERIFY(list.contains(ImageFilter::Lighten));
    QVERIFY(list.contains(ImageFilter::BlueTone));
    QVERIFY(list.contains(ImageFilter::GreyTone));
}

void TestImageFilter::noneIsIdentity() {
    const QImage src = makeSolid(qRgba(180, 40, 200, 128));
    const QImage out = applyFilter(ImageFilter::None, src);
    QCOMPARE(out, src);
}

void TestImageFilter::nullImagePassesThrough() {
    const QImage null;
    QVERIFY(applyFilter(ImageFilter::Sepia, null).isNull());
    QVERIFY(applyFilter(QStringLiteral("greyscale"), null).isNull());
}

void TestImageFilter::greyscaleProducesEqualChannels() {
    const QImage src = makeSolid(qRgba(200, 100, 50, 255));
    const QImage out = applyFilter(ImageFilter::Greyscale, src);
    QCOMPARE(r(out, 0, 0), g(out, 0, 0));
    QCOMPARE(g(out, 0, 0), b(out, 0, 0));
    // Rec.709: 0.2126*200 + 0.7152*100 + 0.0722*50 = 117.73 → 117.
    QCOMPARE(r(out, 0, 0), 117);
}

void TestImageFilter::greyscalePreservesAlpha() {
    const QImage src = makeSolid(qRgba(10, 20, 30, 77));
    const QImage out = applyFilter(ImageFilter::Greyscale, src);
    QCOMPARE(a(out, 0, 0), 77);
}

void TestImageFilter::blackAndWhiteSnapsToZeroOrFullWhite() {
    // Luma of this pixel is ~17 → below 128 → black.
    const QImage darkSrc = makeSolid(qRgba(20, 20, 10, 255));
    const QImage darkOut = applyFilter(ImageFilter::BlackAndWhite, darkSrc);
    QCOMPARE(r(darkOut, 0, 0), 0);
    QCOMPARE(g(darkOut, 0, 0), 0);
    QCOMPARE(b(darkOut, 0, 0), 0);

    // Luma of this pixel is ~200 → above 128 → white.
    const QImage brightSrc = makeSolid(qRgba(200, 200, 200, 255));
    const QImage brightOut = applyFilter(ImageFilter::BlackAndWhite, brightSrc);
    QCOMPARE(r(brightOut, 0, 0), 255);
    QCOMPARE(g(brightOut, 0, 0), 255);
    QCOMPARE(b(brightOut, 0, 0), 255);
}

void TestImageFilter::sepiaShiftsNeutralGreyWarm() {
    const QImage src = makeSolid(qRgba(128, 128, 128, 255));
    const QImage out = applyFilter(ImageFilter::Sepia, src);
    // Classic sepia matrix: R coefficients sum to 1.351, G to 1.203,
    // B to 0.937 — so neutral grey becomes R > G > B (warm tone).
    QVERIFY(r(out, 0, 0) > g(out, 0, 0));
    QVERIFY(g(out, 0, 0) > b(out, 0, 0));
}

void TestImageFilter::lightenAddsBoostAndClampsAtWhite() {
    const QImage mid = makeSolid(qRgba(100, 100, 100, 255));
    const QImage lit = applyFilter(ImageFilter::Lighten, mid);
    QCOMPARE(r(lit, 0, 0), 164);
    QCOMPARE(g(lit, 0, 0), 164);
    QCOMPARE(b(lit, 0, 0), 164);

    // Near-white must clamp instead of wrapping.
    const QImage nearWhite = makeSolid(qRgba(240, 240, 240, 255));
    const QImage clamped = applyFilter(ImageFilter::Lighten, nearWhite);
    QCOMPARE(r(clamped, 0, 0), 255);
    QCOMPARE(g(clamped, 0, 0), 255);
    QCOMPARE(b(clamped, 0, 0), 255);
}

void TestImageFilter::blueTonePushesBlueAboveOthers() {
    const QImage src = makeSolid(qRgba(128, 128, 128, 255));
    const QImage out = applyFilter(ImageFilter::BlueTone, src);
    QVERIFY(b(out, 0, 0) > g(out, 0, 0));
    QVERIFY(g(out, 0, 0) > r(out, 0, 0));
}

void TestImageFilter::greyToneIsGentlerThanGreyscale() {
    const QImage src = makeSolid(qRgba(200, 100, 50, 255));
    const QImage grey = applyFilter(ImageFilter::Greyscale, src);
    const QImage tone = applyFilter(ImageFilter::GreyTone, src);
    // The tone filter biases warm — blue drops below green drops below red.
    QVERIFY(r(tone, 0, 0) >= b(tone, 0, 0));
    QVERIFY(g(tone, 0, 0) >= b(tone, 0, 0));
    // The tone's red channel should sit close to but not above flat greyscale
    // (coefficient 0.98 on luma).
    QVERIFY(r(tone, 0, 0) <= r(grey, 0, 0));
}

void TestImageFilter::applyByIdMatchesApplyByEnum() {
    const QImage src = makeSolid(qRgba(200, 100, 50, 200));
    for (auto f : allFilters()) {
        const QImage viaEnum = applyFilter(f, src);
        const QImage viaId = applyFilter(filterId(f), src);
        QCOMPARE(viaEnum, viaId);
    }
}

QTEST_MAIN(TestImageFilter)
#include "test_image_filter.moc"
