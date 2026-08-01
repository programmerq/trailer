#import "ReducedMotion.h"

#import <AppKit/AppKit.h>

namespace trailer::platform {

bool prefersReducedMotionFromOS() {
    // Available since macOS 10.12; this is the actual system-level
    // "Reduce Motion" toggle (System Settings > Accessibility >
    // Display > Reduce Motion), not a proxy.
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

} // namespace trailer::platform
