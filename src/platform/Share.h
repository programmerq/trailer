#pragma once

#include <QString>

class QWidget;

namespace trailer {

// Cross-platform share-sheet interface. The macOS implementation
// (Share.mm) drives NSSharingServicePicker, which gives the user
// the same Mail / Messages / AirDrop / Add-to-Photos / Save-to-
// Files menu they get from any other native app's Share button.
// On Linux and Windows, ShareService::isAvailable() returns false
// for v1 — a future iteration can wire xdg-email or the WinShare
// API. Callers should hide the Share menu (or grey it out) when
// isAvailable() is false rather than calling shareFile() and
// getting a no-op.
class ShareService {
  public:
    // True when the platform exposes a native share picker we can
    // present. Always false on non-macOS for v1.
    static bool isAvailable();

    // Open the OS share picker for `filePath`. On macOS the picker
    // is anchored to `anchorWidget`'s NSView so it appears as a
    // popover next to the menu item the user clicked. The function
    // returns immediately; the picker is non-modal and dismisses
    // itself.
    static void shareFile(const QString &filePath, QWidget *anchorWidget);
};

} // namespace trailer
