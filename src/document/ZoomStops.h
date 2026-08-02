#pragma once

#include <cmath>

namespace trailer {

// Zoom-ladder stops ("detents").
//
// Zoom in / zoom out step by a fixed geometric ratio (kZoomStep, 1.25 — see
// ImageAdapter.cpp / PdfAdapter.cpp). A pure geometric ladder is
// size-independent, which is why it was chosen, but it has one sharp edge:
// once the factor leaves the exact powers of the ratio, there is no way back
// to a *specific* factor by tapping the key. That bit a real user.
//
// Owner dogfooding report, 2026-08-02 (macOS Retina): a pasted 2x window
// screenshot opened at 100% and rendered 2x too large (its dpr could not be
// recovered — see util/CaptureScale.h). Correcting it by hand walked
// 1.0 -> 0.8 -> 0.64 -> 0.512, i.e. **51%**, so the image was resampled and
// blurry instead of sitting crisp at the exact 1:2 device mapping it needed.
// 50% was unreachable.
//
// The fix is two detents the ladder snaps to:
//
//   * 1.0 — Actual Size. Always meaningful; without it you can leave 100%
//     and never tap your way back to it exactly.
//   * pixelExactZoomFactor(imageDpr, screenDpr) — the factor at which one
//     source device pixel of the image lands on exactly one device pixel of
//     the screen showing it. At that factor the view can hand the source
//     pixels straight through with no resample (see buildDisplayPixmap), so
//     it is the only crisp stop for an image whose dpr differs from its
//     screen's.
//
// A detent captures a step only when it is the nearest rung to where that
// step would land (see steppedZoomFactor below for the exact rule), so the
// ladder is otherwise untouched: in the common case where the image and the
// screen share a dpr, pixelExact == 1.0 and the behaviour is bit-identical
// to the pure geometric ladder.

// The logical zoom factor at which one image device pixel == one screen
// device pixel.
//
// The view's zoom factor is LOGICAL: a factor of 1.0 draws the image at its
// device-independent size (raw px / imageDpr) in logical points, and a
// logical point covers screenDpr device pixels. So the drawn device-pixel
// count is rawPx * factor * screenDpr / imageDpr, and that equals rawPx
// exactly when factor == imageDpr / screenDpr.
//
// Examples: a dpr-1 image on a 2x Retina screen is pixel-exact at 0.5 (the
// owner's case); a dpr-2 screenshot on a dpr-1 screen is pixel-exact at 2.0;
// a matched pair is pixel-exact at 1.0, i.e. Actual Size already is crisp.
//
// Returns 1.0 for non-positive / non-finite inputs so a caller that has not
// yet been given a screen degrades to the Actual-Size detent alone.
inline double pixelExactZoomFactor(double imageDpr, double screenDpr) {
    if (!(imageDpr > 0.0) || !(screenDpr > 0.0) || !std::isfinite(imageDpr) ||
        !std::isfinite(screenDpr)) {
        return 1.0;
    }
    return imageDpr / screenDpr;
}

// Tolerance for "is this factor already at a detent". Zoom factors live in
// [0.05, 32]; 1e-9 is far below any difference a user could perceive or a
// step could produce, and far above double rounding noise on that range.
inline constexpr double kZoomStopEpsilon = 1e-9;

// The next zoom factor when the user taps zoom in / zoom out.
//
// `current` is the current logical factor, `step` the geometric ratio
// (> 1), and `pixelExact` the factor from pixelExactZoomFactor() above.
// Returns `current * step` (or `current / step`), except that a detent
// captures the step when it is the *nearest rung* to where that plain step
// would land — which is the case in either of two situations:
//
//   * the step would land within half a step of the detent (a ratio of
//     sqrt(step); on a log scale, closer to the detent than to the next
//     geometric rung either side), or
//   * the step would jump clean over the detent, which can happen when the
//     factor arrived from a fit mode rather than from the ladder.
//
// A "crossing only" rule is NOT enough, and getting that wrong is what the
// owner actually hit: stepping out from 100% on a 2x screen goes 0.8, 0.64,
// 0.512 — 0.512 never crosses the 0.5 detent, it stops just short of it, so
// a crossing rule would still have parked the user on the blurry 51%.
//
// `current` sitting exactly on a detent never re-snaps to it, so the ladder
// always moves. Bounds clamping is the caller's job (applyScale /
// applyZoomFactor already clamp to kZoomMin/kZoomMax).
inline double steppedZoomFactor(double current, bool zoomIn, double step, double pixelExact) {
    if (!(current > 0.0) || !(step > 1.0) || !std::isfinite(current) || !std::isfinite(step))
        return current;

    const double plain = zoomIn ? current * step : current / step;
    // Half a step, as a ratio: the midpoint between two rungs in log space.
    const double halfStep = std::sqrt(step);
    const double detents[] = {1.0, pixelExact};

    // Pass 1 — detents the plain step would jump clean over. One of these
    // must be taken (a step may not skip a detent); take the nearest to
    // where we are, so we never leapfrog one detent to reach a farther one.
    bool haveOvershot = false;
    double overshot = plain;
    // Pass 2 — detents the plain step lands close to. Best (smallest) ratio
    // wins, and only if it beats the half-step threshold. (Named
    // `nearDetent`, not `near`: <windows.h> has historically #define'd bare
    // `near` / `far` as empty macros, and this header is compiled in the
    // Windows lanes.)
    double nearDetent = plain;
    double bestRatio = halfStep;

    for (const double detent : detents) {
        if (!(detent > 0.0) || !std::isfinite(detent))
            continue;
        // Already sitting on it — never re-snap, or the ladder sticks.
        if (std::abs(current - detent) < kZoomStopEpsilon)
            continue;
        // Only detents in the direction of travel are candidates.
        if (zoomIn ? detent <= current : detent >= current)
            continue;

        if (zoomIn ? plain > detent : plain < detent) {
            if (!haveOvershot || (zoomIn ? detent < overshot : detent > overshot)) {
                haveOvershot = true;
                overshot = detent;
            }
            continue;
        }
        const double ratio = plain > detent ? plain / detent : detent / plain;
        if (ratio < bestRatio) {
            bestRatio = ratio;
            nearDetent = detent;
        }
    }
    return haveOvershot ? overshot : nearDetent;
}

} // namespace trailer
