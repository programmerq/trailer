#include "SamSegmentDialog.h"

#include "ml/SamSession.h"

#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>

#include <cmath>

namespace trailer {

namespace {

// Find the nearest point in a set to `target`, returning its index or
// -1 if the set is empty. Distance is in source-image pixel space.
int nearestIndex(const QVector<QPoint>& pts, QPoint target) {
    int best = -1;
    double bestD2 = std::numeric_limits<double>::max();
    for (int i = 0; i < pts.size(); ++i) {
        const double dx = pts[i].x() - target.x();
        const double dy = pts[i].y() - target.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

}  // namespace

SamPromptCanvas::SamPromptCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

void SamPromptCanvas::setSource(const QImage& source) {
    m_source = source;
    m_mask = QImage();
    m_polygon = QPolygon();
    m_positives.clear();
    m_negatives.clear();
    update();
}

void SamPromptCanvas::setMask(const QImage& maskGrayscale) {
    m_mask = maskGrayscale;
    update();
}

void SamPromptCanvas::setPolygon(const QPolygon& poly) {
    m_polygon = poly;
    update();
}

void SamPromptCanvas::clearPrompts() {
    m_positives.clear();
    m_negatives.clear();
    m_mask = QImage();
    m_polygon = QPolygon();
    update();
    emit prompted();
}

QPoint SamPromptCanvas::canvasToSource(QPoint canvasPt) const {
    if (m_source.isNull() || width() <= 0 || height() <= 0) return {};
    // The image is drawn centered with aspect ratio preserved. Compute
    // the draw rect the same way paintEvent does, then invert.
    const QSize target = m_source.size().scaled(
        size(), Qt::KeepAspectRatio);
    const int offsetX = (width() - target.width()) / 2;
    const int offsetY = (height() - target.height()) / 2;
    if (target.width() <= 0 || target.height() <= 0) return {};
    const double fx = static_cast<double>(canvasPt.x() - offsetX) /
                      target.width();
    const double fy = static_cast<double>(canvasPt.y() - offsetY) /
                      target.height();
    if (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0) return {-1, -1};
    return QPoint(
        static_cast<int>(std::round(fx * m_source.width())),
        static_cast<int>(std::round(fy * m_source.height())));
}

void SamPromptCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(32, 32, 32));
    if (m_source.isNull()) return;

    const QSize target = m_source.size().scaled(
        size(), Qt::KeepAspectRatio);
    const QRect drawRect(
        (width() - target.width()) / 2,
        (height() - target.height()) / 2,
        target.width(), target.height());

    p.drawImage(drawRect, m_source);

    // Mask overlay — translucent blue where foreground.
    if (m_showMask && !m_mask.isNull() &&
        m_mask.size() == m_source.size()) {
        QImage tint(m_mask.size(), QImage::Format_ARGB32);
        tint.fill(Qt::transparent);
        for (int y = 0; y < tint.height(); ++y) {
            auto* dst = reinterpret_cast<QRgb*>(tint.scanLine(y));
            const uchar* src = m_mask.constScanLine(y);
            for (int x = 0; x < tint.width(); ++x) {
                if (src[x]) dst[x] = qRgba(64, 128, 255, 96);
            }
        }
        p.drawImage(drawRect, tint);
    }

    // Polygon outline (Smart Lasso mode).
    if (m_showPolygon && !m_polygon.isEmpty()) {
        QPolygonF scaled;
        scaled.reserve(m_polygon.size());
        const double sx =
            static_cast<double>(drawRect.width()) / m_source.width();
        const double sy =
            static_cast<double>(drawRect.height()) / m_source.height();
        for (const QPoint& pt : m_polygon) {
            scaled.append(QPointF(drawRect.x() + pt.x() * sx,
                                  drawRect.y() + pt.y() * sy));
        }
        QPen outline(QColor(255, 200, 40), 2);
        outline.setJoinStyle(Qt::RoundJoin);
        p.setPen(outline);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(scaled);
    }

    // Prompt markers — green for positive, red for negative.
    auto drawMarker = [&](QPoint srcPt, QColor colour) {
        const double sx =
            static_cast<double>(drawRect.width()) / m_source.width();
        const double sy =
            static_cast<double>(drawRect.height()) / m_source.height();
        const QPointF c(drawRect.x() + srcPt.x() * sx,
                        drawRect.y() + srcPt.y() * sy);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(colour);
        p.drawEllipse(c, 6.0, 6.0);
    };
    for (const QPoint& pt : m_positives) drawMarker(pt, QColor(64, 192, 80));
    for (const QPoint& pt : m_negatives) drawMarker(pt, QColor(220, 64, 64));
}

void SamPromptCanvas::mousePressEvent(QMouseEvent* event) {
    if (m_source.isNull()) return;
    const QPoint srcPt = canvasToSource(event->pos());
    if (srcPt.x() < 0) return;  // clicked outside image bounds

    if (event->button() == Qt::RightButton) {
        // Remove nearest prompt.
        const int ip = nearestIndex(m_positives, srcPt);
        const int in = nearestIndex(m_negatives, srcPt);
        double dp = std::numeric_limits<double>::max();
        double dn = std::numeric_limits<double>::max();
        if (ip >= 0) {
            const double dx = m_positives[ip].x() - srcPt.x();
            const double dy = m_positives[ip].y() - srcPt.y();
            dp = dx * dx + dy * dy;
        }
        if (in >= 0) {
            const double dx = m_negatives[in].x() - srcPt.x();
            const double dy = m_negatives[in].y() - srcPt.y();
            dn = dx * dx + dy * dy;
        }
        if (dp == std::numeric_limits<double>::max() &&
            dn == std::numeric_limits<double>::max()) {
            return;
        }
        if (dp <= dn) m_positives.remove(ip);
        else m_negatives.remove(in);
    } else if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            m_negatives.append(srcPt);
        } else {
            m_positives.append(srcPt);
        }
    } else {
        return;
    }
    update();
    emit prompted();
}

SamSegmentDialog::SamSegmentDialog(Mode mode, const QImage& source,
                                   SamSession* session, QWidget* parent)
    : QDialog(parent), m_mode(mode), m_source(source), m_session(session) {
    setWindowTitle(mode == Mode::InstantAlpha
                       ? tr("Instant Alpha")
                       : tr("Smart Lasso"));
    auto* layout = new QVBoxLayout(this);

    m_hint = new QLabel(tr(
        "Click on the object you want to select. Shift-click adds an "
        "exclusion point. Right-click removes the nearest point."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_canvas = new SamPromptCanvas(this);
    m_canvas->setSource(source);
    m_canvas->setShowPolygon(mode == Mode::SmartLasso);
    m_canvas->setShowMask(mode == Mode::InstantAlpha);
    connect(m_canvas, &SamPromptCanvas::prompted,
            this, &SamSegmentDialog::onPrompted);
    layout->addWidget(m_canvas, /*stretch=*/1);

    auto* buttonRow = new QHBoxLayout();
    m_clearButton = new QPushButton(tr("Clear Points"), this);
    m_clearButton->setEnabled(false);
    connect(m_clearButton, &QPushButton::clicked,
            this, &SamSegmentDialog::onClearClicked);
    buttonRow->addWidget(m_clearButton);
    buttonRow->addStretch();

    auto* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = box->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonRow->addWidget(box);
    layout->addLayout(buttonRow);

    // Prepare the encoder up-front so the first click is responsive.
    // This blocks the UI briefly (80-120 ms on CPU); acceptable as a
    // one-shot setup cost.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool prepared = m_session && m_session->prepare(source);
    QApplication::restoreOverrideCursor();
    if (!prepared) {
        // Disable the whole canvas if we couldn't prepare. We still
        // show the dialog so the error path has a "Cancel" exit.
        m_canvas->setEnabled(false);
        m_hint->setText(tr("Could not prepare the segmentation model "
                           "for this image. Cancel and try again."));
    }
}

void SamSegmentDialog::onClearClicked() {
    m_canvas->clearPrompts();
    m_clearButton->setEnabled(false);
    m_okButton->setEnabled(false);
}

void SamSegmentDialog::onPrompted() {
    m_clearButton->setEnabled(m_canvas->hasAnyPrompt());
    rebuildPreview();
}

void SamSegmentDialog::rebuildPreview() {
    if (!m_session) return;
    const auto positives = m_canvas->positives();
    const auto negatives = m_canvas->negatives();
    if (positives.isEmpty() && negatives.isEmpty()) {
        m_canvas->setMask(QImage());
        m_canvas->setPolygon(QPolygon());
        m_result = QImage();
        m_polygon = QPolygon();
        m_okButton->setEnabled(false);
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QImage mask = m_session->segment(positives, negatives);
    QApplication::restoreOverrideCursor();

    if (mask.isNull()) {
        m_canvas->setMask(QImage());
        m_canvas->setPolygon(QPolygon());
        m_result = QImage();
        m_polygon = QPolygon();
        m_okButton->setEnabled(false);
        return;
    }

    m_canvas->setMask(mask);
    if (m_mode == Mode::SmartLasso) {
        m_polygon = m_session->contourFromLastMask();
        m_canvas->setPolygon(m_polygon);
        m_okButton->setEnabled(!m_polygon.isEmpty());
    } else {
        m_result = m_session->applyAsAlpha(m_source);
        m_okButton->setEnabled(!m_result.isNull());
    }
}

}  // namespace trailer
