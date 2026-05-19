#include "BackgroundCandidateScorer.h"

#include "CancellationToken.h"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace trailer {

namespace {

// Squash a non-negative raw measurement into [0,1] with a "soft
// saturation" curve: at `pivot` the output is ~0.5, at 2× pivot the
// output is ~0.8, asymptotic to 1. Equivalent to v/(v+pivot). The
// nonlinearity matters because raw Sobel / variance numbers grow
// without an obvious upper bound — we want "more is better, but with
// diminishing returns" so a single huge gradient (e.g. a stark border
// on an otherwise flat doc) doesn't single-handedly tip the score.
inline float squash(float v, float pivot) {
    if (v <= 0.0f)
        return 0.0f;
    const float p = std::max(pivot, 1e-6f);
    return v / (v + p);
}

// Tunables. The pivots and weights are derived from runs on a small
// internal sample (a flat scan, a bird photo, and a portrait). They
// are intentionally conservative: borderline images stay below the
// recommendation threshold so the badge only appears when the signal
// is solid.
//
// Pivots are the points at which each sub-component hits 0.5 after
// squash() — i.e. "this much of this signal counts as a clear yes."
constexpr float kEdgePivot = 12.0f;         // mean |Sobel| / 8 → photos ~10..40
constexpr float kSaturationPivot = 1500.0f; // S variance, S in [0..255]
constexpr float kBimodalityPivot = 0.15f;   // bimodality score in [0..1]

// Minimum image edge in pixels to be worth scoring. Below this the
// statistics get too noisy to be useful (small thumbnails inflate
// edge density without telling us anything about the source).
constexpr int kMinShortEdge = 32;

QImage normaliseForScoring(const QImage &input) {
    // Convert to ARGB32 once; all three passes index pixels via QRgb.
    // Scaled down to a deterministic working size so the score is
    // independent of the caller's thumbnail dimensions. 128 on the long
    // edge is plenty for our coarse statistics and keeps the work
    // budget under a few milliseconds even on older laptops.
    constexpr int kWorkingEdge = 128;
    QImage rgb = input.convertToFormat(QImage::Format_ARGB32);
    if (rgb.width() > kWorkingEdge || rgb.height() > kWorkingEdge) {
        rgb = rgb.scaled(kWorkingEdge, kWorkingEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return rgb;
}

// Mean Sobel magnitude across the working image. The 3×3 kernel uses
// the standard separable form; we operate on the luminance plane so
// pure-colour gradients (a blue sky next to a red apple) still count
// as edges.
float meanEdgeMagnitude(const QImage &img) {
    const int w = img.width();
    const int h = img.height();
    if (w < 3 || h < 3)
        return 0.0f;

    // Cache luminance into a flat byte buffer. Cheaper than recomputing
    // qGray per Sobel sample (each interior pixel reads 8 neighbours).
    std::vector<uchar> lum(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        const auto *scan = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        uchar *dst = lum.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        for (int x = 0; x < w; ++x) {
            dst[x] = static_cast<uchar>(qGray(scan[x]));
        }
    }

    double sum = 0.0;
    long count = 0;
    for (int y = 1; y < h - 1; ++y) {
        const uchar *r0 = lum.data() + static_cast<size_t>(y - 1) * static_cast<size_t>(w);
        const uchar *r1 = lum.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        const uchar *r2 = lum.data() + static_cast<size_t>(y + 1) * static_cast<size_t>(w);
        for (int x = 1; x < w - 1; ++x) {
            const int gx = -static_cast<int>(r0[x - 1]) - 2 * static_cast<int>(r1[x - 1]) -
                           static_cast<int>(r2[x - 1]) + static_cast<int>(r0[x + 1]) +
                           2 * static_cast<int>(r1[x + 1]) + static_cast<int>(r2[x + 1]);
            const int gy = -static_cast<int>(r0[x - 1]) - 2 * static_cast<int>(r0[x]) -
                           static_cast<int>(r0[x + 1]) + static_cast<int>(r2[x - 1]) +
                           2 * static_cast<int>(r2[x]) + static_cast<int>(r2[x + 1]);
            // |Sobel| upper-bounded by ~1140 for an 8-bit image; we
            // normalise into a 0..~32 range to keep the squash pivot
            // intuitive.
            const double mag = std::sqrt(static_cast<double>(gx * gx + gy * gy));
            sum += mag;
            ++count;
        }
    }
    if (count == 0)
        return 0.0f;
    // Divide by 8 to keep the typical photo range comparable to the
    // edge pivot above. The exact scaling doesn't matter — squash()
    // turns whatever raw band we land in into a (0,1) score.
    return static_cast<float>(sum / static_cast<double>(count) / 8.0);
}

// Variance of the HSV S channel across the image. Documents come out
// very low (paper is desaturated); photos with any colour content come
// out comfortably above the pivot.
float saturationVariance(const QImage &img) {
    const int w = img.width();
    const int h = img.height();
    if (w == 0 || h == 0)
        return 0.0f;

    double sum = 0.0;
    double sumSq = 0.0;
    long total = 0;
    for (int y = 0; y < h; ++y) {
        const auto *scan = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QColor c(scan[x]);
            int hh = 0;
            int s = 0;
            int v = 0;
            c.getHsv(&hh, &s, &v);
            // s and v are in [0,255]; treat undefined hue (s==0) as 0.
            sum += static_cast<double>(s);
            sumSq += static_cast<double>(s) * static_cast<double>(s);
            ++total;
        }
    }
    if (total == 0)
        return 0.0f;
    const double mean = sum / static_cast<double>(total);
    const double var = (sumSq / static_cast<double>(total)) - (mean * mean);
    return static_cast<float>(std::max(0.0, var));
}

// Cheap bimodality check on a 32-bin luminance histogram. We find the
// two tallest peaks at least 6 bins apart (so adjacent bins don't both
// count) and return their *combined* fractional mass minus a small
// penalty for unevenness. Smooth gradients have a single dominant peak
// and score low; photos with subject/background separation tend to
// have two clear peaks of comparable height.
float luminanceBimodality(const QImage &img) {
    const int w = img.width();
    const int h = img.height();
    if (w == 0 || h == 0)
        return 0.0f;

    constexpr int kBins = 32;
    std::array<long, kBins> hist{};
    long total = 0;
    for (int y = 0; y < h; ++y) {
        const auto *scan = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int g = qGray(scan[x]);
            const int bin = std::min(kBins - 1, g * kBins / 256);
            ++hist[static_cast<size_t>(bin)];
            ++total;
        }
    }
    if (total == 0)
        return 0.0f;

    // First peak: max bin.
    int peak1 = 0;
    long peak1Count = 0;
    for (int i = 0; i < kBins; ++i) {
        if (hist[static_cast<size_t>(i)] > peak1Count) {
            peak1Count = hist[static_cast<size_t>(i)];
            peak1 = i;
        }
    }
    // Second peak: max bin at least 6 bins away from peak1.
    int peak2 = -1;
    long peak2Count = 0;
    constexpr int kMinSeparation = 6;
    for (int i = 0; i < kBins; ++i) {
        if (std::abs(i - peak1) < kMinSeparation)
            continue;
        if (hist[static_cast<size_t>(i)] > peak2Count) {
            peak2Count = hist[static_cast<size_t>(i)];
            peak2 = i;
        }
    }
    if (peak2 < 0 || peak2Count == 0)
        return 0.0f;

    const double frac1 = static_cast<double>(peak1Count) / static_cast<double>(total);
    const double frac2 = static_cast<double>(peak2Count) / static_cast<double>(total);
    // Two-peak combined mass, weighted to penalise the case where
    // peak2 is much smaller than peak1 (which is what a smooth
    // gradient with a long thin tail tends to produce).
    const double balance = std::min(frac1, frac2) / std::max(frac1, frac2);
    const double score = (frac1 + frac2) * balance;
    return static_cast<float>(std::clamp(score, 0.0, 1.0));
}

} // namespace

BackgroundCandidateScorer::Components
BackgroundCandidateScorer::score(const QImage &thumbnail, const CancellationToken *cancel) {
    Components out;
    if (thumbnail.isNull())
        return out;
    if (std::min(thumbnail.width(), thumbnail.height()) < kMinShortEdge)
        return out;
    if (CancellationToken::isCancelled(cancel))
        return out;

    const QImage working = normaliseForScoring(thumbnail);
    if (working.isNull())
        return out;
    if (CancellationToken::isCancelled(cancel))
        return out;

    const float rawEdge = meanEdgeMagnitude(working);
    out.edge = squash(rawEdge, kEdgePivot);
    if (CancellationToken::isCancelled(cancel))
        return out;

    const float rawSat = saturationVariance(working);
    out.saturation = squash(rawSat, kSaturationPivot);
    if (CancellationToken::isCancelled(cancel))
        return out;

    const float rawBimodal = luminanceBimodality(working);
    out.bimodality = squash(rawBimodal, kBimodalityPivot);

    out.combined = (out.edge + out.saturation + out.bimodality) / 3.0f;
    return out;
}

} // namespace trailer
