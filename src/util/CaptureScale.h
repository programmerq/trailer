#pragma once

#include <QImage>
#include <QList>
#include <QSize>

namespace trailer {

// One connected screen, reduced to the two numbers the paste-scale policy
// needs: its device-pixel resolution (logical size * dpr) and its dpr.
struct ScreenScale {
    QSize deviceResolution;
    double dpr = 1.0;
};

// Snapshot of QGuiApplication::screens() as ScreenScale rows. Split out so
// recoverCaptureDpr() below stays a pure function that tests can drive with
// a synthetic screen set on any machine.
QList<ScreenScale> connectedScreenScales();

// Decide the devicePixelRatio to stamp on an image arriving from the
// clipboard (New from Clipboard) or a screen capture.
//
// THE PROBLEM. A bitmap on the clipboard is just pixels. Nothing in the
// pixels says whether they are 1000 points of a 1x screen or 500 points of
// a 2x one, and Trailer has to choose: at Actual Size a wrong choice draws
// a Retina window screenshot at twice its true size. There is no signal
// that always exists, so this function uses only signals that are
// *declared* by the source, and answers 1.0 — "no HiDPI claim" — when none
// is. It deliberately does not guess from the image's size or shape.
//
// THE POLICY, in order:
//
//   1. The QImage already carries dpr > 1. The source told us outright;
//      honour it. (Rare — most clipboard and PNG round-trips drop it.)
//   2. `declaredScale` > 1 and it equals a connected screen's dpr. This is
//      the scale the platform clipboard itself declares for its current
//      image — see platform/ClipboardScale.h. macOS screenshots ride the
//      pasteboard as 144-dpi bitmaps, i.e. 2 pixels per point, and that is
//      exactly how Preview knows to open them at half their pixel size.
//      The "equals a connected screen's dpr" clamp is what keeps this from
//      becoming a guess: an arbitrary print-resolution image (300 dpi ->
//      4.167) matches no screen and is left alone; a screen capture's
//      declared scale is by construction one of the attached screens'.
//   3. The raw pixel size exactly equals a connected screen's device
//      resolution — a whole-screen grab. Stamp that screen's dpr.
//   4. Otherwise 1.0.
//
// KNOWN LIMITS, stated rather than papered over:
//
//   * On Windows and Linux no declared scale reaches Qt from the clipboard
//     today (see platform/ClipboardScale_stub.cpp), so a HiDPI *window* or
//     *region* screenshot pasted there still lands at 1.0 and opens twice
//     its true size. The remedy is the pixel-exact zoom stop
//     (document/ZoomStops.h), which puts it one keystroke away and renders
//     it unresampled. That is an honest partial answer, not a fix.
//   * A genuine 144-dpi scan pasted on a 2x screen matches rule 2 and opens
//     at half size. This is the same answer Preview gives — 144 dpi really
//     does declare 2 pixels per point — so the behaviour is consistent with
//     the reference app rather than novel.
//   * QImage::dotsPerMeterX() is deliberately NOT consulted as a fallback
//     source for rule 2. Qt seeds it from the platform's logical DPI when
//     the file carries no pHYs/resolution chunk, so "no metadata" is
//     indistinguishable from a real 72-dpi claim, and on a host whose
//     logical DPI happens to be 2x the 72-dpi baseline every ordinary paste
//     would be silently halved. The platform seam in rule 2 reads the
//     pasteboard's own bitmap representation instead, which reports a true
//     1.0 when nothing was declared.
double recoverCaptureDpr(const QImage &image, double declaredScale,
                         const QList<ScreenScale> &screens);

} // namespace trailer
