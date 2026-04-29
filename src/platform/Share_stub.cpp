#include "Share.h"

#include <QWidget>

// Linux / Windows stub. v1 returns isAvailable() == false so the
// File → Share menu can be hidden. A future iteration can wire
// xdg-email --attach on Linux and the IDataTransferManager /
// WinShare API on Windows.

namespace trailer {

bool ShareService::isAvailable() {
    return false;
}

void ShareService::shareFile(const QString& /*filePath*/,
                             QWidget* /*anchorWidget*/) {
    // No-op. Callers should gate on isAvailable() before reaching
    // here; we still implement the symbol so linkers don't complain.
}

}  // namespace trailer
