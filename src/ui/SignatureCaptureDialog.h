#pragma once

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QWidget>
#include <vector>

class QLineEdit;
class QPushButton;
class QTabWidget;

namespace trailer {

// Drawing canvas used by SignatureCaptureDialog's "Draw" tab.
// Captures mouse / stylus / Force Touch trackpad strokes as
// per-sample (position, pressure) records and renders them into an
// alpha-channel QImage at the canvas's native size — which is also
// the size the cached PNG is written at, so signatures don't get
// rasterised to a 320×120 thumbnail and pixelate when stamped on
// a high-DPI page.
//
// Pressure source priority:
//   1. QTabletEvent::pressure() — Wacom / Bamboo / Surface Pen
//      and any other absolute-coordinate stylus that surfaces
//      through Qt's tablet protocol. Always 0..1.
//   2. QPointerEvent::pressure() on the QMouseEvent — covers
//      Apple Force Touch trackpads (Cocoa backend reports
//      NSEvent.pressure here in Qt 6.5+) and any pressure-aware
//      pointer that arrives via the mouse path.
//   3. Default 0.5 — a regular mouse with no pressure data.
//
// Tablet input also bypasses OS pointer acceleration (QTabletEvent
// reports raw absolute device coords), so a Wacom stroke matches
// the user's hand motion without jiggling. Force Touch and mouse
// strokes get a small moving-average smoothing pass on release to
// hide quantisation jitter.
class SignatureCanvas : public QWidget {
    Q_OBJECT
public:
    explicit SignatureCanvas(QWidget* parent = nullptr);

    // Return the signature as an RGBA image, cropped to the tight
    // bounding box of the ink. If nothing has been drawn returns a
    // null image.
    QImage render() const;

    void clear();
    bool isEmpty() const { return m_strokes.empty(); }
    // True when the most recently added stroke received pressure
    // data > 0 from at least one sample. Surface for tests + UI to
    // confirm a pressure-aware device was driving the canvas.
    bool lastStrokeUsedPressure() const { return m_lastStrokeUsedPressure; }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

private:
    struct Sample { QPointF pos; qreal pressure; };
    // Map a 0..1 raw pressure to a stroke width in canvas pixels.
    // Cubic curve gives a wider dynamic range than the previous
    // linear: 0.0 → 1px (light hairline), 0.5 → ~3px (mid),
    // 1.0 → ~7px (heavy).
    static qreal widthForPressure(qreal pressure);
    void beginStroke(const QPointF& pos, qreal pressure);
    void extendStroke(const QPointF& pos, qreal pressure);
    void finishStroke();

    // A stroke is a run of (position, pressure) samples. Drawing
    // interpolates pen width between consecutive samples via
    // widthForPressure.
    std::vector<std::vector<Sample>> m_strokes;
    std::vector<Sample>* m_current = nullptr;
    bool m_lastStrokeUsedPressure = false;

    // Computed on each sample-add so render() can crop tightly.
    QRectF m_bounds;
};

// Dialog for capturing a new signature. Two tabs:
//   Draw    — SignatureCanvas, user draws with mouse / stylus
//   Import  — pick a PNG/JPEG from disk, shown as preview
//
// accept() is only enabled once a non-empty signature is ready. The
// caller reads the result via image() and label().
class SignatureCaptureDialog : public QDialog {
    Q_OBJECT
public:
    explicit SignatureCaptureDialog(QWidget* parent = nullptr);

    QImage image() const { return m_result; }
    QString label() const;

private slots:
    void onDrawingChanged();
    void onClearClicked();
    void onBrowseClicked();
    void onTabChanged(int index);

private:
    void updateAcceptEnabled();

    QTabWidget* m_tabs = nullptr;
    SignatureCanvas* m_canvas = nullptr;
    QPushButton* m_clearButton = nullptr;

    QWidget* m_importPreview = nullptr;  // holds m_importImageLabel
    QImage m_importImage;

    QLineEdit* m_label = nullptr;
    QPushButton* m_okButton = nullptr;

    QImage m_result;
};

}  // namespace trailer
