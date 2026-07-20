#include "ui/FileChangeBanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace trailer {

FileChangeBanner::FileChangeBanner(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("fileChangeBanner"));
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(true);
    // A muted amber attention strip — visible but not alarming, consistent
    // with the app's native, low-drama surfaces. Theme-neutral inline style so
    // it reads on both light and dark without pulling a stylesheet dependency.
    setStyleSheet(QStringLiteral(
        "#fileChangeBanner { background-color: #fff4d6; border-bottom: 1px solid #e6c86a; }"
        "#fileChangeBanner QLabel { color: #5a4a12; }"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 8, 6);
    layout->setSpacing(8);

    m_icon = new QLabel(QStringLiteral("⚠"), this); // warning sign
    layout->addWidget(m_icon);

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    layout->addWidget(m_message, 1);

    m_reloadButton = new QPushButton(tr("Reload (discard my edits)"), this);
    m_keepMineButton = new QPushButton(tr("Keep mine"), this);
    m_compareButton = new QPushButton(tr("Compare"), this);
    m_saveButton = new QPushButton(tr("Save"), this);
    m_dismissButton = new QPushButton(tr("Dismiss"), this);

    // Compare is a G3-honest placeholder: present so the option is discoverable
    // once a diff view exists, but disabled with a tooltip that says WHY —
    // never a lying control that would silently do something else.
    m_compareButton->setEnabled(false);
    m_compareButton->setToolTip(
        tr("Comparing the two versions isn't available yet — a side-by-side "
           "diff view is not built. Use Reload to take the on-disk copy or "
           "Keep mine to overwrite it."));

    layout->addWidget(m_reloadButton);
    layout->addWidget(m_keepMineButton);
    layout->addWidget(m_compareButton);
    layout->addWidget(m_saveButton);
    layout->addWidget(m_dismissButton);

    connect(m_reloadButton, &QPushButton::clicked, this, [this]() {
        emit reloadRequested();
    });
    connect(m_keepMineButton, &QPushButton::clicked, this, [this]() {
        emit keepMineRequested();
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() { emit saveRequested(); });
    connect(m_dismissButton, &QPushButton::clicked, this, [this]() {
        dismiss();
        emit dismissed();
    });

    applyMode(Mode::Hidden);
}

void FileChangeBanner::applyMode(Mode mode) {
    m_mode = mode;
    switch (mode) {
    case Mode::Hidden:
        hide();
        return;
    case Mode::Conflict:
        m_message->setText(
            tr("This file was changed by another program while you had unsaved edits."));
        m_reloadButton->setVisible(true);
        m_keepMineButton->setVisible(true);
        m_compareButton->setVisible(true);
        m_saveButton->setVisible(false);
        break;
    case Mode::Deleted:
        m_message->setText(
            tr("This file was deleted on disk. Your edits are still open — Save to recreate it."));
        m_reloadButton->setVisible(false);
        m_keepMineButton->setVisible(false);
        m_compareButton->setVisible(false);
        m_saveButton->setVisible(true);
        break;
    }
    show();
}

void FileChangeBanner::showConflict() { applyMode(Mode::Conflict); }

void FileChangeBanner::showDeleted() { applyMode(Mode::Deleted); }

void FileChangeBanner::dismiss() { applyMode(Mode::Hidden); }

QString FileChangeBanner::messageText() const { return m_message->text(); }

bool FileChangeBanner::reloadEnabled() const {
    return m_reloadButton->isVisibleTo(const_cast<FileChangeBanner *>(this)) &&
           m_reloadButton->isEnabled();
}
bool FileChangeBanner::keepMineEnabled() const {
    return m_keepMineButton->isVisibleTo(const_cast<FileChangeBanner *>(this)) &&
           m_keepMineButton->isEnabled();
}
bool FileChangeBanner::compareEnabled() const { return m_compareButton->isEnabled(); }
bool FileChangeBanner::saveEnabled() const {
    return m_saveButton->isVisibleTo(const_cast<FileChangeBanner *>(this)) &&
           m_saveButton->isEnabled();
}
QString FileChangeBanner::compareTooltip() const { return m_compareButton->toolTip(); }

void FileChangeBanner::clickReloadForTest() { emit reloadRequested(); }
void FileChangeBanner::clickKeepMineForTest() { emit keepMineRequested(); }
void FileChangeBanner::clickSaveForTest() { emit saveRequested(); }

} // namespace trailer
