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
};

struct AnnotationStyle {
    QColor stroke = QColor(220, 30, 30);
    QColor fill = QColor(0, 0, 0, 0);  // transparent by default
    double strokeWidth = 2.0;
    int fontPointSize = 12;
};

// Geometry is stored in document-native coordinates:
//   - Images: pixels in the source image.
//   - PDFs:   points on the page, origin at top-left (we translate to PDF's
//             bottom-left when serialising to /Annot).
struct Annotation {
    int id = 0;
    int page = 0;
    AnnotationType type = AnnotationType::Rectangle;
    QRectF bounds;                // primary geometry; for Ink this is the bbox
    std::vector<QPointF> points;  // for Ink and Line endpoints
    QString text;                 // Text / Note / and any user-attached comment
    AnnotationStyle style;
};

}  // namespace trailer
