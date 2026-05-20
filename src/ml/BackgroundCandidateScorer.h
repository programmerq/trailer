#pragma once

#include "CancellationToken.h"

#include <QImage>

namespace trailer {

// Cheap CPU heuristic for "does this image look like a good candidate
// for background removal?" — designed to drive a subtle UI hint next
// to Tools → Remove Background…  without ever loading U²-Net itself.
// Running ML to decide whether to surface ML would be wasted compute;
// instead we squeeze three observations out of the existing thumbnail:
//
//   1. Edge density (Sobel) — photos of objects and creatures carry a
//      lot of edge energy along the subject's silhouette, while flat
//      document scans concentrate edges in narrow text bands and most
//      of the page is empty. We measure the *mean magnitude* of the
//      Sobel response over the whole thumbnail.
//
//   2. Saturation variance (HSV) — documents are dominantly desaturated
//      (printer black on paper white). A photo varies wildly across
//      its pixels. The variance of the HSV S channel discriminates the
//      two cleanly.
//
//   3. Luminance bimodality (a cheap two-mode check on a 32-bin
//      histogram) — many photos have a clear subject vs. background
//      separation visible as two luminance modes; a gradient/sky/scan
//      smears across a single mode. We score the gap between the two
//      tallest non-adjacent histogram peaks normalised by the total
//      pixel count.
//
// The three sub-scores are normalised to [0,1] (via empirically chosen
// soft saturation points) and averaged. A score >= kRecommendThreshold
// (currently 0.50 — see the rationale block at the constant itself for
// why) is "good candidate"; below that the badge stays off and the
// user just sees an unannotated menu entry.
//
// This is a tasteful nudge, not a gate. The user can still invoke
// Remove Background on any image — the heuristic only decides whether
// to *highlight* the option. False negatives are fine; false positives
// (badge showing up on a flat document) are the worse outcome, so the
// threshold is tuned conservatively toward "boring images get nothing."
class BackgroundCandidateScorer {
  public:
    // Combined score in [0,1]. Returns 0 when `thumbnail` is null or
    // too small to score reliably (< 32 px on the short side). Safe to
    // call from any thread; pure compute, no Qt event-loop reliance.
    //
    // `cancel` is honoured between the three sub-scores so the
    // MlScheduler can abandon a queued scoring pass when the document
    // closes. Default nullptr keeps existing callers unchanged.
    struct Components {
        float edge = 0.0f;       // Sobel mean magnitude after squashing.
        float saturation = 0.0f; // HSV S variance after squashing.
        float bimodality = 0.0f; // Two-peak luminance histogram gap.
        float combined = 0.0f;   // Mean of the three (zero on null in).
    };

    static Components score(const QImage &thumbnail, const CancellationToken *cancel = nullptr);

    // Threshold above which we surface the badge. Exposed as a public
    // constant so the menu plumbing and tests share one number.
    //
    // 0.50 is intentionally on the loose side of "good candidate" —
    // each sub-score is squashed through a saturating curve, so an
    // average of 0.5 already implies all three signals are meaningfully
    // above their noise floors. Pushed higher (0.6+) the badge starts
    // missing real photos with simple backgrounds. Lower than 0.4 and
    // the badge starts surfacing on busy document scans (text bands +
    // a stamp + a watermark — all three sub-scores get a small kick).
    static constexpr float kRecommendThreshold = 0.50f;

    // Convenience predicate. Returns true iff the combined score
    // clears the recommendation threshold.
    static bool isRecommended(const QImage &thumbnail, const CancellationToken *cancel = nullptr) {
        return score(thumbnail, cancel).combined >= kRecommendThreshold;
    }
};

} // namespace trailer
