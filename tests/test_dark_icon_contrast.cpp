// Regression guard for the dark-mode disabled icon-contrast fix
// (audit 2026-07-15, Fix A). An icon-only toolbar button (magnifier,
// zoom, etc.) has no greyed text label to carry the "disabled" signal,
// so the glyph itself must dim when the action is disabled. Before the
// fix, themedActionIcon registered only QIcon::Normal pixmaps; under a
// dark palette Qt's style-generated disabled fade left the glyph almost
// as bright as enabled (measured disabled peak-luminance ~203 vs enabled
// ~220 — indistinguishable), so the control lied about being inert
// (Gate G3 spirit).
//
// Threshold (G1): under a dark palette, the QIcon::Disabled pixmap peaks
// at luminance <= 150 while the QIcon::Normal pixmap peaks at >= 200, and
// the disabled peak is strictly dimmer than normal.
//
// This test drives the widget palette overload of themedActionIcon, whose
// signature is unchanged by the fix, so the SAME source both FAILS against
// the pre-fix IconHelper (disabled ~203 > 150) and PASSES with it
// (disabled ~127 <= 150).

#include "ui/IconHelper.h"

#include <QIcon>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QWidget>
#include <QtTest/QtTest>

class TestDarkIconContrast : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void disabledPixmapIsHonestlyDimmerUnderDarkPalette();

  private:
    // Peak (brightest opaque) perceptual luminance across a pixmap.
    static int peakLuminance(const QPixmap &pm);
};

void TestDarkIconContrast::initTestCase() {
    // trailer_core is a static lib; force the qrc init object to link so
    // the :/icons/actions/*.svg resources resolve at runtime.
    Q_INIT_RESOURCE(trailer);
}

int TestDarkIconContrast::peakLuminance(const QPixmap &pm) {
    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    int peak = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            if (qAlpha(px) < 128)
                continue; // ignore near-transparent antialiasing fringe
            const int lum =
                (299 * qRed(px) + 587 * qGreen(px) + 114 * qBlue(px)) / 1000;
            peak = std::max(peak, lum);
        }
    }
    return peak;
}

void TestDarkIconContrast::disabledPixmapIsHonestlyDimmerUnderDarkPalette() {
    // A representative dark palette: light window text (enabled glyph),
    // a dim grey disabled foreground (the disabled-text convention).
    QPalette dark;
    dark.setColor(QPalette::WindowText, QColor(220, 220, 220));
    dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));

    QWidget host;
    host.setPalette(dark);

    // view-zoom-in is a real icon-only toolbar button (the magnifier
    // family from the audit). Drive the widget overload — its signature is
    // identical pre- and post-fix.
    const QIcon icon = trailer::themedActionIcon(
        QStringLiteral(":/icons/actions/view-zoom-in.svg"), &host);
    QVERIFY(!icon.isNull());

    const QPixmap normalPm =
        icon.pixmap(QSize(36, 36), QIcon::Normal, QIcon::Off);
    const QPixmap disabledPm =
        icon.pixmap(QSize(36, 36), QIcon::Disabled, QIcon::Off);
    QVERIFY(!normalPm.isNull());
    QVERIFY(!disabledPm.isNull());

    const int normalPeak = peakLuminance(normalPm);
    const int disabledPeak = peakLuminance(disabledPm);

    // Enabled glyph reads bright.
    QVERIFY2(normalPeak >= 200,
             qPrintable(QStringLiteral("normal peak luminance %1 < 200")
                            .arg(normalPeak)));
    // Disabled glyph reads honestly dim (fails ~203 on the pre-fix
    // style-fade path; ~127 with the explicit Disabled pixmap).
    QVERIFY2(disabledPeak <= 150,
             qPrintable(QStringLiteral("disabled peak luminance %1 > 150 — "
                                       "disabled glyph is not honestly dimmed")
                            .arg(disabledPeak)));
    // And strictly dimmer than enabled.
    QVERIFY2(disabledPeak < normalPeak,
             qPrintable(QStringLiteral("disabled peak %1 not < normal peak %2")
                            .arg(disabledPeak)
                            .arg(normalPeak)));
}

QTEST_MAIN(TestDarkIconContrast)
#include "test_dark_icon_contrast.moc"
