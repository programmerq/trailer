#include "DockRecents.h"

// Linux / Windows stub: a no-op. Recents stay reachable via Trailer's own
// File > Open Recent menu (RecentFiles) on every platform; only the
// macOS-native "not-running" Dock surface is missing here, so the
// feature's SHAPE differs per OS but it is not dropped.
// T-64: Windows has no Jump List of recent files.

namespace trailer {

void DockRecents::syncSystemRecents(const QStringList & /*pathsMostRecentFirst*/) {
    // Nothing to register off macOS.
}

QStringList DockRecents::systemRecentsForTesting() {
    // Nothing to read back off macOS.
    return {};
}

} // namespace trailer
