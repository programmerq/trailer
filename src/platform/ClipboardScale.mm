#import "ClipboardScale.h"

#import <AppKit/AppKit.h>

#include <cmath>

namespace trailer::platform {

double clipboardImageDeclaredScale(const QSize &expectedPixelSize) {
    if (expectedPixelSize.isEmpty())
        return 0.0;

    @autoreleasepool {
        NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
        if (!pasteboard)
            return 0.0;

        // Prefer PNG (macOS screenshots put one on the pasteboard and its
        // pHYs chunk is the resolution NSBitmapImageRep reads); fall back to
        // TIFF, which is the flavour Qt itself consumes.
        NSData *data = [pasteboard dataForType:NSPasteboardTypePNG];
        if (!data)
            data = [pasteboard dataForType:NSPasteboardTypeTIFF];
        if (!data)
            return 0.0;

        NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:data];
        if (!rep)
            return 0.0;

        const NSInteger pixelsWide = rep.pixelsWide;
        const NSInteger pixelsHigh = rep.pixelsHigh;
        // Guard against the clipboard changing between the caller's read and
        // this one: a scale taken from a different image is worse than none.
        if (pixelsWide != expectedPixelSize.width() || pixelsHigh != expectedPixelSize.height())
            return 0.0;

        // -size is the rep's size in POINTS, derived from the bitmap's
        // recorded resolution (72 dpi -> points == pixels; a 144-dpi Retina
        // screenshot -> points == pixels / 2). A bitmap that declares no
        // resolution reports points == pixels, i.e. a ratio of 1, which we
        // report as "nothing declared".
        const NSSize pointSize = rep.size;
        if (pointSize.width <= 0.0)
            return 0.0;

        const double scale = static_cast<double>(pixelsWide) / pointSize.width;
        if (!std::isfinite(scale) || scale <= 1.0)
            return 0.0;
        return scale;
    }
}

} // namespace trailer::platform
