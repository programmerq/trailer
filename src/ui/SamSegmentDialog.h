#pragma once

#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QPolygon>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;

namespace trailer {

class SamSession;

// Widget that renders an image and captures click-to-prompt events for
// MobileSAM segmentation. Left-click adds a positive point (include),
// Shift+Left-click adds a negative point (exclude), Right-click
// removes the nearest point. Emits `prompted()` on every change so
// the host can re-run the decoder and update the preview overlay.
class SamPromptCanvas : public QWidget {
    Q_OBJECT
public:
    explicit SamPromptCanvas(QWidget* parent = nullptr);

    void setSource(const QImage& source);
    void setMask(const QImage& maskGrayscale);
    void setPolygon(const QPolygon& poly);
    void setShowPolygon(bool on) { m_showPolygon = on; update(); }
    void setShowMask(bool on)    { m_showMask = on; update(); }

    QVector<QPoint> positives() const { return m_positives; }
    QVector<QPoint> negatives() const { return m_negatives; }
    bool hasAnyPrompt() const {
        return !m_positives.isEmpty() || !m_negatives.isEmpty();
    }
    void clearPrompts();

    // Convert canvas coords (widget px) to source-image coords.
    QPoint canvasToSource(QPoint canvasPt) const;

signals:
    void prompted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return {480, 360}; }

private:
    QImage m_source;
    QImage m_mask;      // grayscale8, same size as m_source
    QPolygon m_polygon; // source-coord polygon
    QVector<QPoint> m_positives;
    QVector<QPoint> m_negatives;
    bool m_showMask = true;
    bool m_showPolygon = false;
};

// Modal dialog that drives a SamSession against a user-provided
// image. Two modes:
//   InstantAlpha — user clicks the object they want to keep, OK
//                  returns that image with non-foreground pixels set
//                  to alpha=0.
//   SmartLasso   — user clicks the object they want to select, OK
//                  returns the QPolygon of the object's contour.
//
// The caller owns both `source` and `session` — this dialog does not
// take ownership of either. `session->prepare(source)` is called
// inside the dialog, so the caller only needs to ensure the model is
// ready.
class SamSegmentDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { InstantAlpha, SmartLasso };

    SamSegmentDialog(Mode mode, const QImage& source,
                     SamSession* session, QWidget* parent = nullptr);

    // Populated when the user clicks OK. In InstantAlpha mode the
    // returned image is ARGB32 same-size as the input, with alpha=0
    // outside the selection. In SmartLasso mode the image is null.
    QImage resultImage() const { return m_result; }

    // Populated when the user clicks OK. Empty in InstantAlpha mode.
    QPolygon resultPolygon() const { return m_polygon; }

private slots:
    void onPrompted();
    void onClearClicked();

private:
    void rebuildPreview();

    Mode m_mode;
    QImage m_source;
    SamSession* m_session;

    SamPromptCanvas* m_canvas = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_okButton = nullptr;

    QImage m_result;
    QPolygon m_polygon;
};

}  // namespace trailer
