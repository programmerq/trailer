#include "util/CaptureScale.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSizeF>

#include <cmath>

namespace trailer {

namespace {

// A declared scale and a screen's dpr are both "how many device pixels per
// point"; they match when they name the same ratio. 1e-6 is loose enough to
// absorb the float division inside a platform's own dpi->scale arithmetic
// (144/72 can arrive as 1.9999999) and far tighter than the gap between any
// two real dpr values (1.0 / 1.25 / 1.5 / 2.0 / 3.0).
constexpr double kDprMatchTolerance = 1e-6;

bool matchesAScreenDpr(double scale, const QList<ScreenScale> &screens) {
    for (const ScreenScale &s : screens) {
        if (std::abs(scale - s.dpr) < kDprMatchTolerance)
            return true;
    }
    return false;
}

} // namespace

QList<ScreenScale> connectedScreenScales() {
    QList<ScreenScale> out;
    const QList<QScreen *> screens = QGuiApplication::screens();
    out.reserve(screens.size());
    for (const QScreen *scr : screens) {
        if (!scr)
            continue;
        ScreenScale s;
        s.dpr = scr->devicePixelRatio();
        s.deviceResolution = (QSizeF(scr->size()) * s.dpr).toSize();
        out.append(s);
    }
    return out;
}

double recoverCaptureDpr(const QImage &image, double declaredScale,
                         const QList<ScreenScale> &screens) {
    // Rule 1 — the image already carries a HiDPI stamp.
    const double stamped = image.devicePixelRatio();
    if (stamped > 1.0 && std::isfinite(stamped))
        return stamped;

    // Rule 2 — the platform clipboard declared a scale, and it names one of
    // the attached screens. See the header for why the "names a screen"
    // clamp is what separates this from a guess.
    if (declaredScale > 1.0 && std::isfinite(declaredScale) &&
        matchesAScreenDpr(declaredScale, screens)) {
        return declaredScale;
    }

    // Rule 3 — a whole-screen grab: raw pixels == some screen's device
    // resolution. Kept from the original heuristic; it is exact, not a
    // guess, but it only ever matched full-screen captures, which is the
    // gap rule 2 exists to close.
    //
    // Ties resolve DOWNWARD, to the lowest dpr among matching screens. Two
    // attached screens can share a device resolution at different scales (a
    // 1920x1080 1x monitor next to a 960x540-point 2x panel), and then a
    // 1920x1080 paste is genuinely ambiguous. Picking the lower dpr means an
    // ambiguous match never shrinks the image — the same "when in doubt,
    // don't scale" stance as rule 4. (The pre-2026-08-02 code took whichever
    // screen QGuiApplication happened to list first, which made the answer
    // depend on monitor enumeration order.)
    const QSize raw = image.size();
    if (!raw.isEmpty()) {
        double best = 0.0;
        for (const ScreenScale &s : screens) {
            if (raw != s.deviceResolution || !(s.dpr > 0.0))
                continue;
            if (best == 0.0 || s.dpr < best)
                best = s.dpr;
        }
        if (best > 1.0)
            return best;
    }

    // Rule 4 — nothing declared a scale. Say so, rather than inventing one:
    // an ordinary paste (a copied logo, a diagram, pixel art) must open at
    // its natural logical size. A blanket "stamp the primary screen's dpr"
    // was tried and reverted for exactly that reason — it halved every
    // ordinary paste on Retina.
    return 1.0;
}

} // namespace trailer
