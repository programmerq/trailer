#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

namespace trailer {

enum class AnnotationTool {
    None,
    Select,
    Rectangle,
    Ellipse,
    Line,
    Arrow,
    Ink,
    Text,
    Note,
    Highlight,
    Underline,
    StrikeOut,
    HighlightShape, // translucent filled rectangle (non-text)
    SpeechBubble,
    ZoomLens,
    Signature, // drag-place a saved signature PNG (§6.4.3)
    Redaction, // paints over a region; flattened permanently on save
};

enum class AnnotationType {
    Rectangle,
    Ellipse,
    Line,
    Arrow,
    Ink,       // freehand stroke (polyline of points)
    Text,      // free-text box
    Note,      // sticky note (small icon + popup text)
    Highlight, // text highlight (PDF text range)
    Underline,
    StrikeOut,
    HighlightShape, // translucent filled shape
    SpeechBubble,   // rounded rect with pointer tail
    ZoomLens,       // circular magnifier
    Signature,      // image-stamp of a saved signature PNG; flattened on save
    Redaction,      // opaque black block; content beneath raster-flattened on save
};

enum class DashStyle {
    Solid,
    Dashed,
    Dotted,
};

struct AnnotationStyle {
    // Neutral charcoal grey by default. Red feels alarmed and
    // pushes every first shape toward looking like a destructive
    // marker; grey reads as a markup tool that the user can
    // recolour to anything else from the Inspector / toolbar
    // swatch on demand.
    QColor stroke = QColor(60, 60, 60);
    QColor fill = QColor(0, 0, 0, 0); // transparent by default
    double strokeWidth = 2.0;
    int fontPointSize = 12;
    DashStyle dash = DashStyle::Solid;
    QString fontFamily;      // empty → painter default
    int fontWeight = 50;     // QFont::Normal
    double zoomFactor = 2.0; // used by ZoomLens
};

// Geometry is stored in document-native coordinates:
//   - Images: pixels in the source image.
//   - PDFs:   points on the page, origin at top-left (we translate to PDF's
//             bottom-left when serialising to /Annot).
struct Annotation {
    int id = 0;
    int page = 0;
    AnnotationType type = AnnotationType::Rectangle;
    QRectF bounds;               // primary geometry; for Ink this is the bbox
    std::vector<QPointF> points; // for Ink and Line endpoints
    // Per-sample pressure, parallel to `points`. Populated for Ink
    // strokes captured from a pressure-aware device (Wacom tablet,
    // Apple Force Touch trackpad). Empty for Line/Arrow and for Ink
    // strokes from a plain mouse — the renderer falls back to the
    // style's stroke width when this is empty.
    std::vector<float> pressures;
    std::vector<QRectF> quads; // per-run rects for Highlight/Underline/StrikeOut
    QString text;              // Text / Note / and any user-attached comment
    // Absolute filesystem path to an image asset used for image-stamp
    // annotations (currently Signature). Empty for everything else.
    // Stored as a path rather than bytes so multiple signature
    // annotations share one PNG on disk and saves don't bloat memory.
    QString imagePath;
    AnnotationStyle style;
};

} // namespace trailer
