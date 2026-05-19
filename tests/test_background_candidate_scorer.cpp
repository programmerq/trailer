// Unit tests for BackgroundCandidateScorer.
//
// The scorer is the CPU heuristic that decides whether the "Remove
// Background looks promising" badge appears next to the Tools menu
// entry. There's no model — we run Sobel + HSV variance + a luminance
// histogram across the existing thumbnail and combine into a single
// 0..1 score.
//
// Tests build fixtures programmatically so we don't ship binary PNGs:
//
//   - "Flat document" — a near-white page with two thin bands of dark
//     text. Edges concentrated in narrow rows; saturation ~0; one
//     luminance mode at the high end. Should score below the
//     recommendation threshold.
//   - "Bird on sky" — a saturated coloured blob on a desaturated
//     gradient. High edge density along the silhouette, high
//     saturation variance, two clear luminance modes. Should score
//     well above the threshold.
//   - "Portrait on busy background" — gradient + radial subject. A
//     middle-of-the-road fixture; the perf guard runs on this and
//     also asserts that the score is *between* the two extremes
//     (the scorer should at least order them correctly).
//
//   - Null / tiny images return zero without crashing.
//
//   - Cancellation: a pre-cancelled token short-circuits before the
//     expensive passes.
//
//   - Perf: a single score() call on a 128×160 thumbnail completes
//     in well under 50 ms. The budget catches accidental introduction
//     of a heavyweight filter (e.g. a blur with too large a kernel).

#include "ml/BackgroundCandidateScorer.h"
#include "ml/CancellationToken.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtTest/QtTest>

#include <cmath>

using namespace trailer;

namespace {

// A simulated document scan: near-white page, two thin bands of
// rectangles representing dark text lines. Saturation near zero.
QImage makeFlatDocumentFixture(int w = 192, int h = 240) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor(248, 248, 248));
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30));
    // Two bands of "text" at 30% and 60% of the height.
    const int textHeight = 6;
    for (int row = 0; row < 2; ++row) {
        const int y = (row == 0 ? h * 30 / 100 : h * 60 / 100);
        // Simulate 6 broken-up word boxes per line.
        for (int word = 0; word < 6; ++word) {
            const int x = 10 + word * (w - 20) / 6;
            const int wWord = (w - 30) / 6 - 4;
            p.drawRect(x, y, wWord, textHeight);
        }
    }
    p.end();
    return img;
}

// A simulated photo: a parrot filling most of the frame. A large
// saturated coloured subject with high-frequency feathers detail
// against a moderately edge-active background. Mirrors the kind of
// snapshot a user would expect to do background-removal on.
QImage makeBirdOnSkyFixture(int w = 192, int h = 144) {
    QImage img(w, h, QImage::Format_ARGB32);
    QPainter p(&img);
    // Background — sky gradient with some hue rotation so the
    // saturation variance is non-trivial.
    for (int y = 0; y < h; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(h - 1);
        const int r = 100 + static_cast<int>(t * 40);
        const int g = 140 + static_cast<int>(t * 60);
        const int b = 220 - static_cast<int>(t * 80);
        p.setPen(QColor(r, g, b));
        p.drawLine(0, y, w, y);
    }
    // Bird — big radial subject with several layered colour patches
    // so the silhouette and the internal "feather" patches both
    // contribute edge energy. Sized to cover roughly 40% of the frame.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    QRadialGradient body(QPointF(w * 0.45, h * 0.55), w * 0.35);
    body.setColorAt(0.0, QColor(240, 80, 30));
    body.setColorAt(0.7, QColor(180, 30, 40));
    body.setColorAt(1.0, QColor(80, 20, 50));
    p.setBrush(body);
    p.drawEllipse(QPointF(w * 0.45, h * 0.55), w * 0.36, h * 0.45);
    // Wing — second colour patch with a hard edge.
    p.setBrush(QColor(30, 140, 80));
    QPainterPath wing;
    wing.moveTo(w * 0.20, h * 0.50);
    wing.cubicTo(w * 0.10, h * 0.70, w * 0.30, h * 0.85, w * 0.55, h * 0.78);
    wing.cubicTo(w * 0.50, h * 0.65, w * 0.40, h * 0.55, w * 0.20, h * 0.50);
    p.drawPath(wing);
    // Feather stripes — adds high-frequency edges across the bird.
    // The pattern is dense enough to push the mean Sobel response
    // above the recommend pivot.
    p.setPen(QPen(QColor(20, 20, 20), 2));
    for (int i = 0; i < 14; ++i) {
        const float t = static_cast<float>(i) / 14.0f;
        const int y0 = static_cast<int>(h * (0.30f + t * 0.55f));
        p.drawLine(static_cast<int>(w * 0.15), y0, static_cast<int>(w * 0.75), y0 + 3);
    }
    // A few angled feathers for variety, plus some saturated patches
    // on the wing so the saturation variance is higher.
    p.setPen(QPen(QColor(255, 220, 30), 2));
    for (int i = 0; i < 6; ++i) {
        const float t = static_cast<float>(i) / 6.0f;
        const int y0 = static_cast<int>(h * (0.50f + t * 0.30f));
        p.drawLine(static_cast<int>(w * 0.25), y0, static_cast<int>(w * 0.65), y0 + 6);
    }
    p.setPen(Qt::NoPen);
    // Beak.
    p.setBrush(QColor(255, 180, 30));
    QPainterPath beak;
    beak.moveTo(w * 0.60, h * 0.35);
    beak.lineTo(w * 0.85, h * 0.40);
    beak.lineTo(w * 0.60, h * 0.50);
    beak.closeSubpath();
    p.drawPath(beak);
    // Eye — small high-contrast detail.
    p.setBrush(QColor(20, 20, 20));
    p.drawEllipse(QPointF(w * 0.55, h * 0.40), w * 0.04, w * 0.04);
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPointF(w * 0.555, h * 0.395), w * 0.012, w * 0.012);
    p.end();
    return img;
}

// A simulated portrait against a busy background. Gradient background
// with a radial subject. Score should land somewhere between the flat
// document and the bird — i.e. the scorer orders the three correctly
// even if the exact middle value is fixture-dependent.
QImage makePortraitFixture(int w = 192, int h = 240) {
    QImage img(w, h, QImage::Format_ARGB32);
    QPainter p(&img);
    // Background — a vertical gradient with some saturation.
    QLinearGradient bgGrad(0, 0, 0, h);
    bgGrad.setColorAt(0.0, QColor(120, 80, 60));
    bgGrad.setColorAt(1.0, QColor(40, 20, 10));
    p.fillRect(img.rect(), bgGrad);
    // Subject — radial gradient (face-shaped).
    p.setRenderHint(QPainter::Antialiasing, true);
    QRadialGradient subj(QPointF(w * 0.5, h * 0.45), w * 0.30);
    subj.setColorAt(0.0, QColor(230, 200, 180));
    subj.setColorAt(0.8, QColor(180, 150, 130));
    subj.setColorAt(1.0, QColor(80, 60, 40, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(subj);
    p.drawEllipse(QPointF(w * 0.5, h * 0.45), w * 0.30, h * 0.32);
    p.end();
    return img;
}

} // namespace

class TestBackgroundCandidateScorer : public QObject {
    Q_OBJECT
  private slots:
    void scoresFlatDocumentBelowThreshold();
    void scoresBirdPhotoAboveThreshold();
    void portraitFallsBetweenFlatAndBird();
    void rejectsTinyImage();
    void rejectsNullImage();
    void prerunCancelReturnsZero();
    void componentsAreBoundedToUnitInterval();
    void fitsWithinPerfBudget();
};

void TestBackgroundCandidateScorer::scoresFlatDocumentBelowThreshold() {
    const QImage doc = makeFlatDocumentFixture();
    const auto c = BackgroundCandidateScorer::score(doc);
    QVERIFY2(c.combined < BackgroundCandidateScorer::kRecommendThreshold,
             qPrintable(QString("flat document scored %1, expected below %2")
                            .arg(c.combined)
                            .arg(BackgroundCandidateScorer::kRecommendThreshold)));
    // Saturation variance in particular should be near zero — paper
    // has no colour.
    QVERIFY2(c.saturation < 0.25f,
             qPrintable(
                 QString("flat doc saturation sub-score %1 unexpectedly high").arg(c.saturation)));
}

void TestBackgroundCandidateScorer::scoresBirdPhotoAboveThreshold() {
    const QImage bird = makeBirdOnSkyFixture();
    const auto c = BackgroundCandidateScorer::score(bird);
    QVERIFY2(c.combined >= BackgroundCandidateScorer::kRecommendThreshold,
             qPrintable(QString("bird-on-sky scored %1, expected >= %2")
                            .arg(c.combined)
                            .arg(BackgroundCandidateScorer::kRecommendThreshold)));
    // The bird fixture has plainly visible edges *and* saturation, so
    // each individual sub-score should clear half.
    QVERIFY2(c.edge > 0.5f, qPrintable(QString("edge sub-score %1 too low").arg(c.edge)));
    QVERIFY2(c.saturation > 0.5f,
             qPrintable(QString("saturation sub-score %1 too low").arg(c.saturation)));
}

void TestBackgroundCandidateScorer::portraitFallsBetweenFlatAndBird() {
    const float flat = BackgroundCandidateScorer::score(makeFlatDocumentFixture()).combined;
    const float portrait = BackgroundCandidateScorer::score(makePortraitFixture()).combined;
    const float bird = BackgroundCandidateScorer::score(makeBirdOnSkyFixture()).combined;
    QVERIFY2(portrait > flat,
             qPrintable(QString("portrait %1 not above flat %2").arg(portrait).arg(flat)));
    QVERIFY2(portrait < bird,
             qPrintable(QString("portrait %1 not below bird %2").arg(portrait).arg(bird)));
}

void TestBackgroundCandidateScorer::rejectsTinyImage() {
    QImage tiny(16, 16, QImage::Format_ARGB32);
    tiny.fill(Qt::white);
    const auto c = BackgroundCandidateScorer::score(tiny);
    QCOMPARE(c.combined, 0.0f);
}

void TestBackgroundCandidateScorer::rejectsNullImage() {
    QImage empty;
    const auto c = BackgroundCandidateScorer::score(empty);
    QCOMPARE(c.combined, 0.0f);
}

void TestBackgroundCandidateScorer::prerunCancelReturnsZero() {
    CancellationToken token;
    token.cancel();
    const auto c = BackgroundCandidateScorer::score(makeBirdOnSkyFixture(), &token);
    QCOMPARE(c.combined, 0.0f);
}

void TestBackgroundCandidateScorer::componentsAreBoundedToUnitInterval() {
    for (const QImage &img :
         {makeFlatDocumentFixture(), makeBirdOnSkyFixture(), makePortraitFixture()}) {
        const auto c = BackgroundCandidateScorer::score(img);
        QVERIFY(c.edge >= 0.0f && c.edge <= 1.0f);
        QVERIFY(c.saturation >= 0.0f && c.saturation <= 1.0f);
        QVERIFY(c.bimodality >= 0.0f && c.bimodality <= 1.0f);
        QVERIFY(c.combined >= 0.0f && c.combined <= 1.0f);
    }
}

void TestBackgroundCandidateScorer::fitsWithinPerfBudget() {
    // 128×160 ARGB thumbnail — the typical size produced by
    // IDocument::renderThumbnail for sidebar consumption. A single
    // scoring pass should comfortably fit under 50 ms even on slow
    // hardware. We measure 5 iterations to dampen one-off jitter
    // (cold caches, system load) and compare the *average*.
    const QImage thumb = makeBirdOnSkyFixture(128, 160);
    constexpr int kIterations = 5;
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < kIterations; ++i) {
        const auto c = BackgroundCandidateScorer::score(thumb);
        Q_UNUSED(c);
    }
    const qint64 elapsed = t.elapsed();
    const double avg = static_cast<double>(elapsed) / kIterations;
    QVERIFY2(avg < 50.0,
             qPrintable(QString("avg scorer time %1 ms over %2 iterations exceeds 50 ms budget")
                            .arg(avg)
                            .arg(kIterations)));
}

QTEST_MAIN(TestBackgroundCandidateScorer)
#include "test_background_candidate_scorer.moc"
