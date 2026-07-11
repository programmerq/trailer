#include "EmptyStateWidget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QStringList>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace trailer {

EmptyStateWidget::EmptyStateWidget(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);

    // Centred column: icon → headline → subtitle → button. The outer
    // layout stretches above and below so the block floats in the
    // vertical centre no matter how tall the window is.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(48, 48, 48, 48);
    outer->addStretch(1);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    // A QStyle standard icon is guaranteed to render under every
    // platform plugin (including offscreen) without depending on the
    // resource bundle being initialised — the empty state must never
    // show a blank hole where the glyph should be.
    const QIcon openIcon = style()->standardIcon(QStyle::SP_DialogOpenButton);
    m_iconLabel->setPixmap(openIcon.pixmap(64, 64));
    outer->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

    outer->addSpacing(16);

    m_headline = new QLabel(tr("Open a file"), this);
    m_headline->setAlignment(Qt::AlignCenter);
    QFont headlineFont = m_headline->font();
    headlineFont.setPointSizeF(headlineFont.pointSizeF() * 1.6);
    headlineFont.setBold(true);
    m_headline->setFont(headlineFont);
    outer->addWidget(m_headline, 0, Qt::AlignHCenter);

    outer->addSpacing(8);

    m_subtitle =
        new QLabel(tr("Drag a PDF or image here, or choose one to get started."), this);
    m_subtitle->setAlignment(Qt::AlignCenter);
    // A one-line subtitle by design; leave word-wrap off so the layout
    // gives it its full single-line width instead of collapsing it to a
    // narrow, overlapping two-line block.
    m_subtitle->setWordWrap(false);
    // Dim the subtitle relative to the headline so the hierarchy reads.
    QPalette subtitlePalette = m_subtitle->palette();
    QColor dim = subtitlePalette.color(QPalette::WindowText);
    dim.setAlphaF(0.65);
    subtitlePalette.setColor(QPalette::WindowText, dim);
    m_subtitle->setPalette(subtitlePalette);
    outer->addWidget(m_subtitle, 0, Qt::AlignHCenter);

    outer->addSpacing(24);

    m_openButton = new QPushButton(tr("Open File…"), this);
    m_openButton->setCursor(Qt::PointingHandCursor);
    connect(m_openButton, &QPushButton::clicked, this, &EmptyStateWidget::openRequested);
    outer->addWidget(m_openButton, 0, Qt::AlignHCenter);

    outer->addStretch(1);
}

void EmptyStateWidget::setDragHighlighted(bool highlighted) {
    if (m_dragHighlight == highlighted)
        return;
    m_dragHighlight = highlighted;
    update();
}

void EmptyStateWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        setDragHighlighted(true);
    }
}

void EmptyStateWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    Q_UNUSED(event);
    setDragHighlighted(false);
}

void EmptyStateWidget::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            const QString local = url.toLocalFile();
            if (!local.isEmpty())
                paths.append(local);
        }
    }
    setDragHighlighted(false);
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        emit filesDropped(paths);
    }
}

void EmptyStateWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Inset the dashed frame so it doesn't clip against the window edge.
    const qreal inset = 24.0;
    const QRectF frame = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    const qreal radius = 16.0;

    QColor borderColor;
    qreal penWidth = 1.5;
    if (m_dragHighlight) {
        // Stronger, accent-tinted border while a drag hovers so the
        // surface visibly reads as "drop here".
        borderColor = palette().color(QPalette::Highlight);
        penWidth = 2.5;
        QColor fill = borderColor;
        fill.setAlphaF(0.08);
        QPainterPath fillPath;
        fillPath.addRoundedRect(frame, radius, radius);
        painter.fillPath(fillPath, fill);
    } else {
        borderColor = palette().color(QPalette::WindowText);
        borderColor.setAlphaF(0.25);
    }

    QPen pen(borderColor);
    pen.setWidthF(penWidth);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({6.0, 5.0});
    painter.setPen(pen);
    painter.drawRoundedRect(frame, radius, radius);
}

} // namespace trailer
