#include "EmptyStateWidget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFont>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace trailer {

EmptyStateWidget::EmptyStateWidget(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);

    // Centred column: icon → headline → subtitle → button → (optional
    // recent list). The outer layout stretches above and below so the
    // block floats in the vertical centre no matter how tall the window
    // is.
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

    // Inline "Recent" list. Modelled on macOS Preview's welcome surface:
    // a short, plain vertical list of file names (no thumbnails, no
    // project-picker chrome). Built empty and hidden; setRecentEntries()
    // populates it and toggles visibility. Kept out of the layout's
    // stretch region so it sits directly under the button.
    m_recentSection = new QWidget(this);
    auto *recentColumn = new QVBoxLayout(m_recentSection);
    recentColumn->setContentsMargins(0, 20, 0, 0);
    recentColumn->setSpacing(2);

    auto *recentHeading = new QLabel(tr("Recent"), m_recentSection);
    recentHeading->setAlignment(Qt::AlignLeft);
    // Dim + slightly smaller than body text so it reads as a quiet
    // section label, not a competing headline.
    QFont headingFont = recentHeading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 0.9);
    recentHeading->setFont(headingFont);
    QPalette headingPalette = recentHeading->palette();
    QColor headingDim = headingPalette.color(QPalette::WindowText);
    headingDim.setAlphaF(0.55);
    headingPalette.setColor(QPalette::WindowText, headingDim);
    recentHeading->setPalette(headingPalette);
    recentColumn->addWidget(recentHeading, 0, Qt::AlignLeft);

    // The entry buttons live in their own layout so setRecentEntries()
    // can rebuild just the list, leaving the heading in place.
    m_recentEntriesLayout = new QVBoxLayout();
    m_recentEntriesLayout->setContentsMargins(0, 0, 0, 0);
    m_recentEntriesLayout->setSpacing(0);
    recentColumn->addLayout(m_recentEntriesLayout);

    m_recentSection->hide();
    outer->addWidget(m_recentSection, 0, Qt::AlignHCenter);

    outer->addStretch(1);
}

void EmptyStateWidget::setRecentEntries(const QList<RecentEntry> &entries) {
    if (!m_recentEntriesLayout || !m_recentSection)
        return;

    // Clear the previous entry buttons. Delete now (not deleteLater): a
    // taken-but-not-yet-deleted button stays a visible child at its old
    // geometry and would ghost under the rebuilt list until the event
    // loop drained. Immediate delete is safe even under re-entrancy —
    // clicking an entry runs openFiles → notifyWindowsRecentChanged →
    // rebuildRecentMenu → setRecentEntries, i.e. this can run *inside* a
    // button's own clicked() emission. QAbstractButton::click() guards its
    // emitter with a QPointer and skips post-emit work once it nulls, so
    // deleting that button here does not use-after-free.
    while (QLayoutItem *item = m_recentEntriesLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }

    const int shown = qMin(entries.size(), kMaxRecentShown);
    for (int i = 0; i < shown; ++i) {
        const RecentEntry &entry = entries.at(i);
        auto *button = new QPushButton(entry.displayName, m_recentSection);
        // Flat, link-style: no push-button chrome, left-aligned text,
        // pointing-hand cursor — reads as a clickable name, not a chunky
        // button, matching Preview's quiet recent list.
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("text-align: left; padding: 2px 0px;"));
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        // The full path is a quiet secondary affordance on hover; the
        // visible label stays the short display name (Preview restraint).
        button->setToolTip(entry.path);
        const QString path = entry.path;
        connect(button, &QPushButton::clicked, this,
                [this, path]() { emit openRecentRequested(path); });
        m_recentEntriesLayout->addWidget(button, 0, Qt::AlignLeft);
    }

    // No lying / empty affordance: hide the whole section (heading
    // included) when there is nothing to list.
    m_recentSection->setVisible(shown > 0);
}

int EmptyStateWidget::recentEntryCount() const {
    return m_recentEntriesLayout ? m_recentEntriesLayout->count() : 0;
}

bool EmptyStateWidget::isRecentSectionVisible() const {
    return m_recentSection && m_recentSection->isVisibleTo(const_cast<EmptyStateWidget *>(this));
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
