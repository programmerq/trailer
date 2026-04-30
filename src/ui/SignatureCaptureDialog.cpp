#include "SignatureCaptureDialog.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QEventPoint>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTabletEvent>
#include <QTabWidget>
#include <QVBoxLayout>

namespace trailer {

// --- SignatureCanvas ------------------------------------------------

SignatureCanvas::SignatureCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StaticContents);
    // Bigger default canvas so signatures get more pixels to work
    // with. The previous 320×120 was tight enough that any cursive
    // ascender / descender clipped against the edge and the saved
    // PNG looked stamp-sized.
    setMinimumSize(640, 200);
    setCursor(Qt::CrossCursor);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    setTabletTracking(true);
}

qreal SignatureCanvas::widthForPressure(qreal pressure) {
    // Cubic curve. Light hairlines stay light, mid-pressure feels
    // like a normal pen, full pressure produces a confident thick
    // line. The previous linear `1 + pressure*5` gave too narrow a
    // dynamic range — both light and heavy strokes looked roughly
    // the same.
    const qreal p = std::clamp(pressure, 0.0, 1.0);
    const qreal shaped = p * p * p;            // 0..1, cubic
    return 1.0 + shaped * 6.0;                 // 1..7 px
}

void SignatureCanvas::clear() {
    m_strokes.clear();
    m_current = nullptr;
    m_lastStrokeUsedPressure = false;
    m_bounds = QRectF();
    update();
    emit changed();
}

void SignatureCanvas::beginStroke(const QPointF& pos, qreal pressure) {
    m_strokes.emplace_back();
    m_current = &m_strokes.back();
    m_lastStrokeUsedPressure = pressure > 0.0;
    m_current->push_back({pos, std::max(pressure, 0.5)});
    m_bounds |= QRectF(pos, QSizeF(1, 1));
    update();
}

void SignatureCanvas::extendStroke(const QPointF& pos, qreal pressure) {
    if (!m_current) return;
    if (pressure > 0.0) m_lastStrokeUsedPressure = true;
    m_current->push_back({pos, std::max(pressure, 0.5)});
    m_bounds |= QRectF(pos, QSizeF(1, 1));
    update();
}

void SignatureCanvas::finishStroke() {
    if (!m_current || m_current->size() < 3) {
        m_current = nullptr;
        emit changed();
        return;
    }
    // 3-point centred moving average on positions to soften
    // pixel-quantisation jitter from mouse / Force Touch input.
    // Tablet pens already deliver smooth absolute coordinates;
    // smoothing them is a no-op for the user but harmless.
    auto& s = *m_current;
    std::vector<Sample> smoothed = s;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        smoothed[i].pos = QPointF(
            (s[i - 1].pos.x() + s[i].pos.x() + s[i + 1].pos.x()) / 3.0,
            (s[i - 1].pos.y() + s[i].pos.y() + s[i + 1].pos.y()) / 3.0);
    }
    s = std::move(smoothed);
    m_current = nullptr;
    update();
    emit changed();
}

QImage SignatureCanvas::render() const {
    if (m_strokes.empty()) return {};

    // Pad the bbox a bit so the stroke doesn't clip at the edge.
    const double pad = 8.0;
    QRectF cropped = m_bounds.adjusted(-pad, -pad, pad, pad);
    cropped = cropped.intersected(QRectF(rect()));
    if (cropped.isEmpty()) cropped = QRectF(rect());

    // Render at 2x the canvas resolution. The signature stamp may
    // land on a 300+ DPI page; doubling here keeps edges sharp
    // without a separate per-stamp re-render at output time.
    constexpr qreal kRenderScale = 2.0;
    const QSize outSize = (cropped.size() * kRenderScale).toSize();
    QImage out(outSize, QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(kRenderScale, kRenderScale);
    p.translate(-cropped.topLeft());

    for (const auto& stroke : m_strokes) {
        if (stroke.empty()) continue;
        for (size_t i = 1; i < stroke.size(); ++i) {
            QPen pen(Qt::black, widthForPressure(stroke[i].pressure),
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.drawLine(stroke[i - 1].pos, stroke[i].pos);
        }
        if (stroke.size() == 1) {
            p.setPen(QPen(Qt::black, widthForPressure(stroke[0].pressure)));
            p.drawPoint(stroke[0].pos);
        }
    }
    return out;
}

void SignatureCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& stroke : m_strokes) {
        for (size_t i = 1; i < stroke.size(); ++i) {
            QPen pen(Qt::black, widthForPressure(stroke[i].pressure),
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.drawLine(stroke[i - 1].pos, stroke[i].pos);
        }
    }
    // Baseline hint so users know where to write.
    p.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    const int y = height() - 24;
    p.drawLine(24, y, width() - 24, y);
}

void SignatureCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    // QPointerEvent (Qt 6) carries per-point pressure — Force Touch
    // trackpads on macOS report a non-zero value via the Cocoa
    // backend. Most plain mice report 0 and we fall back to a
    // constant mid-pressure (0.5) inside beginStroke.
    const qreal pressure = event->points().isEmpty()
        ? 0.0
        : qreal(event->points().first().pressure());
    beginStroke(event->position(), pressure);
}

void SignatureCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!m_current) return;
    // points() yields any sub-events the OS coalesced into this
    // single Qt event — relevant for fast strokes on a Force Touch
    // trackpad where the OS may merge several physical samples per
    // delivered Qt event.
    const auto& pts = event->points();
    if (!pts.isEmpty()) {
        for (const QEventPoint& pt : pts) {
            extendStroke(pt.position(), qreal(pt.pressure()));
        }
    } else {
        extendStroke(event->position(), 0.0);
    }
}

void SignatureCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    finishStroke();
}

void SignatureCanvas::tabletEvent(QTabletEvent* event) {
    switch (event->type()) {
        case QEvent::TabletPress:
            beginStroke(event->position(), event->pressure());
            break;
        case QEvent::TabletMove:
            extendStroke(event->position(), event->pressure());
            break;
        case QEvent::TabletRelease:
            finishStroke();
            break;
        default:
            break;
    }
    event->accept();
}

// --- SignatureCaptureDialog ----------------------------------------

SignatureCaptureDialog::SignatureCaptureDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Capture Signature"));
    resize(480, 320);

    auto* outer = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);

    // Draw tab
    auto* drawPage = new QWidget(this);
    auto* drawLayout = new QVBoxLayout(drawPage);
    m_canvas = new SignatureCanvas(drawPage);
    drawLayout->addWidget(m_canvas, 1);
    auto* drawButtons = new QHBoxLayout;
    m_clearButton = new QPushButton(tr("Clear"), drawPage);
    drawButtons->addStretch(1);
    drawButtons->addWidget(m_clearButton);
    drawLayout->addLayout(drawButtons);
    connect(m_canvas, &SignatureCanvas::changed,
            this, &SignatureCaptureDialog::onDrawingChanged);
    connect(m_clearButton, &QPushButton::clicked,
            this, &SignatureCaptureDialog::onClearClicked);
    m_tabs->addTab(drawPage, tr("Draw"));

    // Import tab
    auto* importPage = new QWidget(this);
    auto* importLayout = new QVBoxLayout(importPage);
    m_importPreview = new QLabel(tr("No image selected."), importPage);
    static_cast<QLabel*>(m_importPreview)->setAlignment(Qt::AlignCenter);
    m_importPreview->setMinimumSize(320, 120);
    m_importPreview->setStyleSheet(QStringLiteral(
        "background: white; border: 1px dashed gray;"));
    importLayout->addWidget(m_importPreview, 1);
    auto* importButtons = new QHBoxLayout;
    auto* browseButton = new QPushButton(tr("Choose Image…"), importPage);
    importButtons->addStretch(1);
    importButtons->addWidget(browseButton);
    importLayout->addLayout(importButtons);
    connect(browseButton, &QPushButton::clicked,
            this, &SignatureCaptureDialog::onBrowseClicked);
    m_tabs->addTab(importPage, tr("Import"));

    connect(m_tabs, &QTabWidget::currentChanged,
            this, &SignatureCaptureDialog::onTabChanged);

    outer->addWidget(m_tabs, 1);

    // Label field
    auto* form = new QFormLayout;
    m_label = new QLineEdit(this);
    m_label->setPlaceholderText(tr("e.g. Jeff, Initials"));
    form->addRow(tr("Label"), m_label);
    outer->addLayout(form);

    // OK / Cancel
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    outer->addWidget(buttons);

    updateAcceptEnabled();
}

QString SignatureCaptureDialog::label() const {
    return m_label->text().trimmed();
}

void SignatureCaptureDialog::onDrawingChanged() {
    if (m_tabs->currentIndex() == 0) {  // Draw
        m_result = m_canvas->render();
    }
    updateAcceptEnabled();
}

void SignatureCaptureDialog::onClearClicked() {
    m_canvas->clear();
    m_result = {};
    updateAcceptEnabled();
}

void SignatureCaptureDialog::onBrowseClicked() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Signature Image"), QString(),
        tr("Images (*.png *.jpg *.jpeg)"));
    if (path.isEmpty()) return;
    QImageReader reader(path);
    QImage img = reader.read();
    if (img.isNull()) return;
    m_importImage = img.convertToFormat(QImage::Format_ARGB32);

    // Preview inside the QLabel.
    if (auto* lbl = qobject_cast<QLabel*>(m_importPreview)) {
        lbl->setPixmap(QPixmap::fromImage(m_importImage)
                           .scaled(lbl->size(), Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
    }
    m_result = m_importImage;
    updateAcceptEnabled();
}

void SignatureCaptureDialog::onTabChanged(int index) {
    // Switching tabs changes which source feeds m_result.
    if (index == 0) {
        m_result = m_canvas->render();
    } else {
        m_result = m_importImage;
    }
    updateAcceptEnabled();
}

void SignatureCaptureDialog::updateAcceptEnabled() {
    if (m_okButton) m_okButton->setEnabled(!m_result.isNull());
}

}  // namespace trailer
