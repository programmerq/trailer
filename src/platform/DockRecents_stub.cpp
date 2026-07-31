#include "DockRecents.h"

// Linux / Windows stub. Neither platform has a Launch-Services-style
// system Recent Documents store for Trailer to register into, so this is
// a no-op — recents stay reachable via Trailer's own File > Open Recent
// menu (RecentFiles) on every platform; only the macOS-native
// "not-running" Dock surface is unavailable here, matching G4 (the
// feature's SHAPE differs per OS; it is not dropped).

namespace trailer {

void DockRecents::syncSystemRecents(const QStringList & /*pathsMostRecentFirst*/) {
    // Nothing to register off macOS.
}

} // namespace trailer
