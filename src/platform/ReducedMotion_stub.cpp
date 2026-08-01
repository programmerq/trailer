#include "ReducedMotion.h"

#include <QApplication>

namespace trailer::platform {

// Neither Windows nor Linux exposes a single OS-level "Reduce Motion"
// flag the way macOS's NSWorkspace does (Windows 11's Settings >
// Accessibility > Visual effects > Animation effects toggle has no
// public Qt-reachable query short of raw Win32 SPI calls; GNOME's
// org.gnome.desktop.interface enable-animations lives behind GSettings,
// which Trailer does not link). As a best-effort proxy, fall back to
// Qt's own animation-enabled style hint -- QApplication seeds
// Qt::UI_AnimateTooltip (and friends) from the desktop's own animation
// preference on the platforms where the QPA backend wires one up, and
// leaves it true elsewhere. This under-detects (a "true" here can still
// mean "no OS signal available", not "the user asked for motion") but
// never over-suppresses a user who deliberately enabled Reduce Motion
// through a route this stub CAN see. Revisit with a real Win32
// SPI_GETCLIENTAREAANIMATION / GSettings query if a dogfooding session
// on either platform finds this proxy misses a real case.
bool prefersReducedMotionFromOS() {
    return !QApplication::isEffectEnabled(Qt::UI_AnimateTooltip);
}

} // namespace trailer::platform
