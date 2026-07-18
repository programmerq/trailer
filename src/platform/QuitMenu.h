#pragma once

class QAction;

namespace trailer {

// macOS-only native menu affordances for "Quit and Keep Windows".
//
// Qt's QAction / native menu bar does not surface the AppKit
// NSMenuItem.alternate flag that drives the standard in-place Option swap
// (`Quit Trailer  ⌘Q` ⇄ `Quit and Keep Windows  ⌥⌘Q`). This shim reaches
// the concrete NSMenuItem to install that alternate, mirroring the
// established src/platform/Share.mm + Share_stub.cpp pattern (CMake wires
// the .mm on macOS and the stub everywhere else).
//
// All Cocoa lives behind Q_OS_MACOS in QuitMenu.mm; the stub is a no-op
// that returns sensible cross-platform defaults so the FUNCTIONAL feature
// (the QuitMode QActions + Application::requestQuit + SessionDraftStore)
// works unchanged without it. The native swap is a display nicety only.
class QuitMenu {
  public:
    // Install a "Quit and Keep Windows" alternate NSMenuItem right after
    // the item backing `quitAction`, carrying keyEquivalent "q" +
    // Command|Option and alternate=YES so holding Option swaps it in place.
    // `keepAction` is triggered when the alternate is chosen, so the
    // native item and the cross-platform QAction drive the same code path.
    // No-op off macOS, or if the native item cannot be located.
    static void installAlternateKeepItem(QAction *quitAction, QAction *keepAction);

    // Read the macOS `NSQuitAlwaysKeepsWindows` user default (System
    // Settings ▸ Desktop & Dock ▸ "Close windows when quitting an app",
    // inverted). When true, a plain Quit takes the keep path and the
    // Option alternate offers the complement (decision record D3-A).
    // Returns false off macOS and when the default is unset.
    static bool osQuitAlwaysKeepsWindows();
};

} // namespace trailer
