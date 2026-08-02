#pragma once

#include <QSize>

namespace trailer::platform {

// The scale factor the system clipboard DECLARES for the image currently on
// it — device pixels per point — or 0.0 when the platform exposes no such
// declaration.
//
// This is the seam that lets a pasted HiDPI screen capture open at its true
// size. A bitmap on the clipboard carries no devicePixelRatio, but the OS
// pasteboard may still record the bitmap's resolution, and for a screen
// capture that resolution *is* the capturing screen's scale. macOS is the
// case that matters today: its screenshots ride the pasteboard as 144-dpi
// bitmaps (2 pixels per 72-dpi point), which is how Preview knows to open a
// Retina screenshot at half its pixel size.
//
// `expectedPixelSize` is the raw pixel size of the QImage the caller
// already pulled off the clipboard. The implementation must return 0.0
// unless the pasteboard's own bitmap has exactly that pixel size — the
// clipboard can change between the caller's read and this one, and a scale
// read from a *different* image would be worse than no answer at all.
//
// Never returns a value below 1.0 for a real answer: a bitmap that declares
// no resolution reports 1 pixel per point, which is "nothing declared", and
// is reported as 0.0.
//
// Implemented per-platform:
//   - macOS (ClipboardScale.mm): NSPasteboard's TIFF/PNG representation,
//     via NSBitmapImageRep's pixelsWide-vs-size ratio.
//   - Windows / Linux (ClipboardScale_stub.cpp): 0.0 — see that file for
//     why there is nothing honest to read there yet.
double clipboardImageDeclaredScale(const QSize &expectedPixelSize);

} // namespace trailer::platform
