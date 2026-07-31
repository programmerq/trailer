#include "DockRecents.h"

#import <AppKit/AppKit.h>

// macOS system Recent-Documents bridge. Guarded by the CMake wiring (this
// .mm is compiled only on APPLE; DockRecents_stub.cpp is used elsewhere),
// mirroring src/platform/Share.mm and src/platform/QuitMenu.mm. All Cocoa
// is confined here.
//
// NSDocumentController is normally the backbone of a document-based
// (NSDocument-subclassing) Cocoa app; Trailer is not one — it manages its
// own documents (src/document/) and its own File > Open Recent menu
// (RecentFiles). But NSDocumentController's recent-documents bookkeeping
// (noteNewRecentDocumentURL: / clearRecentDocuments:) is independently
// useful here: it's exactly what Launch Services persists to disk and
// reads back to populate a Dock icon's right-click recents menu even when
// the owning app isn't running, per Apple's NSDocumentController docs
// ("Managing the Open Recent Menu"). Calling these two methods from a
// non-document app to drive Dock/Finder recents (without ever adopting
// NSDocument) is the documented, supported use of the class for exactly
// this purpose.

namespace trailer {

void DockRecents::syncSystemRecents(const QStringList &pathsMostRecentFirst) {
    NSDocumentController *controller = [NSDocumentController sharedDocumentController];

    // Full resync, not an incremental append: clear first so a path that
    // fell out of Trailer's own recents (Clear Menu, pushed past the cap)
    // doesn't linger in the system list forever. clearRecentDocuments:
    // takes a sender id that's unused by the implementation; nil is the
    // documented call shape.
    [controller clearRecentDocuments:nil];

    // noteNewRecentDocumentURL: inserts (or moves) a URL to the FRONT of
    // the system list, so noting oldest-first leaves the list's front
    // (most-recent Dock-menu position) holding pathsMostRecentFirst's
    // first entry once the loop completes. The caller (see DockRecents.h)
    // has already capped/ordered/existence-filtered this list, so the
    // only defensive check left here is against an empty string.
    for (auto it = pathsMostRecentFirst.crbegin(); it != pathsMostRecentFirst.crend(); ++it) {
        const QString &path = *it;
        if (path.isEmpty())
            continue;

        NSString *nsPath = [NSString stringWithUTF8String:path.toUtf8().constData()];
        if (!nsPath)
            continue;
        NSURL *url = [NSURL fileURLWithPath:nsPath];
        if (url)
            [controller noteNewRecentDocumentURL:url];
    }
}

} // namespace trailer
