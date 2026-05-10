#include "Share.h"

#include <QFileInfo>
#include <QString>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace trailer {

bool ShareService::isAvailable() { return true; }

void ShareService::shareFile(const QString &filePath, QWidget *anchorWidget) {
    if (filePath.isEmpty() || !QFileInfo(filePath).exists()) {
        return;
    }

    NSString *nsPath = [NSString stringWithUTF8String:filePath.toUtf8().constData()];
    NSURL *url = [NSURL fileURLWithPath:nsPath];
    if (!url)
        return;

    NSSharingServicePicker *picker = [[NSSharingServicePicker alloc] initWithItems:@[ url ]];

    // QWidget::winId() returns the WId of the underlying NSView on
    // macOS. The reinterpret_cast collapses the WId-as-quintptr to a
    // raw pointer; the __bridge cast tells ARC that no ownership
    // change is happening — Qt continues to own the view.
    NSView *anchorView = nullptr;
    if (anchorWidget) {
        const WId winId = anchorWidget->winId();
        anchorView = (__bridge NSView *)reinterpret_cast<void *>(winId);
    }

    if (anchorView) {
        // Anchor the popover on the bottom-left corner of the
        // anchor widget so it pops down naturally from a menu
        // item.
        const NSRect bounds = [anchorView bounds];
        const NSRect anchor = NSMakeRect(NSMinX(bounds), NSMinY(bounds), 1, 1);
        [picker showRelativeToRect:anchor ofView:anchorView preferredEdge:NSRectEdgeMinY];
    } else {
        // Fallback: present on the key window if we can't resolve
        // an anchor view. The picker centres itself on screen.
        NSWindow *keyWindow = [[NSApplication sharedApplication] keyWindow];
        if (keyWindow) {
            NSView *contentView = [keyWindow contentView];
            const NSRect b = [contentView bounds];
            [picker showRelativeToRect:NSMakeRect(NSMidX(b), NSMidY(b), 1, 1)
                                ofView:contentView
                         preferredEdge:NSRectEdgeMinY];
        }
    }
}

} // namespace trailer
