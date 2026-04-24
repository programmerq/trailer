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

// Small drawing canvas used by SignatureCaptureDialog's "Draw" tab.
// Captures mouse/stylus strokes into a QImage with alpha, so the
// saved signature carries transparency into the PDF.
//
// Supports pressure via QTabletEvent when the user is drawing with a
// stylus. Mouse input falls back to a constant pen width.
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

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

private:
    struct Point { QPointF pos; qreal width; };
    // A stroke is a run of points with per-point width (for pressure
    // sensitivity). Drawing interpolates between consecutive points.
    std::vector<std::vector<Point>> m_strokes;
    std::vector<Point>* m_current = nullptr;

    // Computed on each stroke-add so render() can crop tightly.
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
