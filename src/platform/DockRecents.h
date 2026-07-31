#pragma once

#include <QStringList>

namespace trailer {

// Bridges Trailer's own RecentFiles model to macOS's system-level Recent
// Documents store (NSDocumentController), which is what Launch Services
// uses to render a per-app recents menu when the user right/Control-clicks
// the Dock icon. That system store is a SEPARATE list from Trailer's own —
// see Application::refreshDockRecents (src/app/Application.cpp), which
// keeps them in sync but never merges them. Concretely, right-clicking the
// Trailer Dock icon works two ways:
//
//   - While Trailer IS running: Application builds a plain QMenu from the
//     same RecentFiles list and calls QMenu::setAsDockMenu() (pure Qt, no
//     Cocoa needed — though Qt only declares that one method under
//     Q_OS_MACOS, so that single call is ifdef'd; see
//     Application::refreshDockRecents for the full picture).
//   - While Trailer is NOT running: nothing in the process is alive to
//     serve a menu, so the Dock instead asks Launch Services for whatever
//     THIS class last registered via noteNewRecentDocumentURL:, which
//     macOS persists to disk on Trailer's behalf. That registration is
//     the entire purpose of this class.
//
// Mirrors the established src/platform/Share.h / QuitMenu.h pattern: all
// Cocoa lives in DockRecents.mm (APPLE-only in CMakeLists.txt);
// DockRecents_stub.cpp is a no-op everywhere else, so callers never need
// an #ifdef Q_OS_MACOS of their own.
class DockRecents {
  public:
    // The owner asked for "the 10 most recent files" (see the PR this
    // shipped in); this is that literal spec value, not an independently
    // hand-tuned constant, so it carries no separate tuning rationale
    // (PHILOSOPHY's "hand-tuned values" rule doesn't apply — there is no
    // range that was tried, only the one number requested).
    static constexpr int kMaxSystemRecents = 10;

    // Replace the macOS system Recent Documents list with exactly
    // `pathsMostRecentFirst`. The caller (Application::refreshDockRecents)
    // is responsible for capping to kMaxSystemRecents, ordering
    // most-recent-first, and filtering to paths that still exist on disk
    // (RecentFiles::existingEntries does all three — see its header
    // comment for why the existence filter matters here but not in
    // Trailer's own in-app Open Recent menu). This function trusts that
    // list as-is; it does no further filtering of its own.
    //
    // The existing system list is cleared first so this call fully
    // RESYNCS rather than only ever appending: a file the user removed
    // from Trailer's own recents (Clear Menu) or pushed past the cap no
    // longer lingers in the Dock/Launch-Services list either.
    //
    // No-op off macOS.
    static void syncSystemRecents(const QStringList &pathsMostRecentFirst);
};

} // namespace trailer
