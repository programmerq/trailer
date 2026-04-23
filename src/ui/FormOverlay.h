#pragma once

#include "document/PdfEditor.h"

#include <QWidget>
#include <functional>
#include <vector>

namespace trailer {

// Overlays interactive Qt widgets over AcroForm fields in the PDF
// viewport. Sits as a sibling of AnnotationOverlay on the viewport
// widget; form-field widgets are positioned using the same
// docToView coordinate mapping PdfDocument already exposes.
//
// Lifecycle: PdfDocument::createView creates one FormOverlay per view
// and connects scroll/zoom signals to relayout(). MainWindow hides or
// shows the entire overlay to toggle form-filling mode.
class FormOverlay : public QWidget {
    Q_OBJECT

public:
    explicit FormOverlay(QWidget* parent = nullptr);

    // Coordinate mapping: converts a point in doc-native space
    // (PDF points, top-left origin) on the given page to viewport
    // pixels. Matches the signature used by AnnotationOverlay.
    using DocToView = std::function<QPointF(QPointF docPt, int page)>;
    void setDocumentToView(DocToView fn);

    // Returns the page's point size (width, height in PDF points).
    using PageSizeFn = std::function<QSizeF(int page)>;
    void setPageSize(PageSizeFn fn);

    // Replace the displayed fields. Clears all child widgets and
    // creates new ones. Call whenever the document or the active page
    // changes.
    void setFields(const std::vector<FormField>& fields);

signals:
    // Emitted whenever a field widget changes its value. The form-
    // filling client (PdfDocument) should call setFormFieldValue(id,
    // value) in response so the change is persisted on save.
    void fieldValueChanged(int fieldId, const QString& newValue);

public slots:
    // Repositions all field widgets to match the current scroll/zoom
    // state. Connect to verticalScrollBar::valueChanged,
    // horizontalScrollBar::valueChanged, and zoomFactorChanged.
    void relayout();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void clearWidgets();
    QWidget* createWidgetForField(const FormField& field);
    // Convert a PDF-coords rect (bottom-left origin) on `page` to a
    // viewport QRect (top-left origin, pixels).
    QRect fieldRectToViewport(const FormField& field) const;

    DocToView m_docToView;
    PageSizeFn m_pageSize;
    std::vector<FormField> m_fields;
    std::vector<std::pair<int, QWidget*>> m_widgets;  // {fieldId, widget*}
};

}  // namespace trailer
