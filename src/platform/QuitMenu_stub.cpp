#include "QuitMenu.h"

// Linux / Windows stub for the macOS "Quit and Keep Windows" native menu
// affordances. Off macOS there is no NSMenuItem.alternate to install and no
// NSQuitAlwaysKeepsWindows default to read, so both entry points are inert:
// installAlternateKeepItem does nothing (the cross-platform QAction still
// carries the ⌥⌘Q shortcut and works), and osQuitAlwaysKeepsWindows reports
// false so requestQuit's default branch stays prompt-and-close-clean.

namespace trailer {

void QuitMenu::installAlternateKeepItem(QAction * /*quitAction*/, QAction * /*keepAction*/) {
    // No native menu chrome to touch off macOS.
}

bool QuitMenu::osQuitAlwaysKeepsWindows() {
    return false;
}

} // namespace trailer
