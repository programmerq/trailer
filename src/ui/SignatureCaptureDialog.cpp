#include "SignatureCaptureDialog.h"

#include <QDialogButtonBox>
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
    setMinimumSize(320, 120);
    setCursor(Qt::CrossCursor);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    setTabletTracking(true);
}

void SignatureCanvas::clear() {
    m_strokes.clear();
    m_current = nullptr;
    m_bounds = QRectF();
    update();
    emit changed();
}

QImage SignatureCanvas::render() const {
    if (m_strokes.empty()) return {};

    // Pad the bbox a bit so the stroke doesn't clip at the edge.
    const double pad = 6.0;
    QRectF cropped = m_bounds.adjusted(-pad, -pad, pad, pad);
    // Clamp to canvas so negative coords don't propagate.
    cropped = cropped.intersected(QRectF(rect()));
    if (cropped.isEmpty()) cropped = QRectF(rect());

    QImage out(cropped.size().toSize(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(-cropped.topLeft());
    p.setPen(QPen(Qt::black, 2.0,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    for (const auto& stroke : m_strokes) {
        if (stroke.empty()) continue;
        // Draw each stroke as a single path where the pen width
        // follows the per-point width. For mouse input the widths are
        // constant so this collapses to a plain polyline.
        for (size_t i = 1; i < stroke.size(); ++i) {
            QPen pen(Qt::black, stroke[i].width,
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.drawLine(stroke[i - 1].pos, stroke[i].pos);
        }
        if (stroke.size() == 1) {
            p.setPen(QPen(Qt::black, stroke[0].width));
            p.drawPoint(stroke[0].pos);
        }
    }
    return out;
}

void SignatureCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, 2.0,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const auto& stroke : m_strokes) {
        for (size_t i = 1; i < stroke.size(); ++i) {
            QPen pen(Qt::black, stroke[i].width,
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.drawLine(stroke[i - 1].pos, stroke[i].pos);
        }
    }
    // Baseline hint so users know where to write.
    p.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    const int y = height() - 20;
    p.drawLine(20, y, width() - 20, y);
}

void SignatureCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    m_strokes.emplace_back();
    m_current = &m_strokes.back();
    m_current->push_back({event->position(), 2.0});
    m_bounds |= QRectF(event->position(), QSizeF(1, 1));
    update();
}

void SignatureCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!m_current) return;
    m_current->push_back({event->position(), 2.0});
    m_bounds |= QRectF(event->position(), QSizeF(1, 1));
    update();
}

void SignatureCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    m_current = nullptr;
    emit changed();
}

void SignatureCanvas::tabletEvent(QTabletEvent* event) {
    // Stylus events: use pressure (0..1) to modulate line width.
    const qreal w = 1.0 + event->pressure() * 5.0;
    switch (event->type()) {
        case QEvent::TabletPress:
            m_strokes.emplace_back();
            m_current = &m_strokes.back();
            m_current->push_back({event->position(), w});
            m_bounds |= QRectF(event->position(), QSizeF(1, 1));
            update();
            break;
        case QEvent::TabletMove:
            if (m_current) {
                m_current->push_back({event->position(), w});
                m_bounds |= QRectF(event->position(), QSizeF(1, 1));
                update();
            }
            break;
        case QEvent::TabletRelease:
            m_current = nullptr;
            emit changed();
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
