#include "ClipboardScale.h"

namespace trailer::platform {

// Windows and Linux: no declared scale is available, so say so.
//
// Windows' clipboard image flavours are CF_DIB / CF_DIBV5. A DIB header
// does carry biXPelsPerMeter, but the Windows screenshot paths (PrintScreen,
// Snipping Tool) write it as 0 or the desktop's own logical DPI rather than
// the capturing monitor's scale factor, so it does not distinguish a 2x
// capture from a 1x one — reading it would manufacture an answer, not
// recover one.
//
// On Linux the clipboard is an X11/Wayland selection whose image flavours
// (image/png, image/bmp) do carry a resolution chunk, but Qt hands the
// caller an already-decoded QImage whose dotsPerMeter has been seeded from
// the platform's logical DPI when the source declared none — so, as
// util/CaptureScale.h explains, "no metadata" and "72 dpi" are
// indistinguishable there and the number cannot be trusted.
//
// Returning 0.0 leaves recoverCaptureDpr() on its remaining rules: an
// already-stamped dpr, or an exact whole-screen device-resolution match.
// A HiDPI window/region screenshot pasted on these platforms therefore
// still opens at dpr 1; the pixel-exact zoom stop (document/ZoomStops.h) is
// the one-keystroke correction, and is documented as such.
//
// Revisit if a dogfooding session on either platform finds a source that
// declares a trustworthy scale.
double clipboardImageDeclaredScale(const QSize &) { return 0.0; }

} // namespace trailer::platform
