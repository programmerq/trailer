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

    // Both action labels state their CONSEQUENCE (CF-6): Reload discards the
    // user's edits; Keep mine keeps the user's version and the file is
    // overwritten only by their next explicit Save — no write happens on click.
    m_reloadButton = new QPushButton(tr("Reload (discard my edits)"), this);
    m_keepMineButton = new QPushButton(tr("Keep mine (Save overwrites the file)"), this);
    m_compareButton = new QPushButton(tr("Compare"), this);
    m_saveButton = new QPushButton(tr("Save"), this);
    m_dismissButton = new QPushButton(tr("Dismiss"), this);

    // Visually-weighted default (CF-6): "Keep mine" is the recommended,
    // non-destructive-until-save choice, so it is the primary/default button
    // (platform-native default affordance + an accent fill so the weighting is
    // visible in the banner). Reload is a normal secondary button; Dismiss is
    // flat/passive since it just hides the banner and decides nothing.
    m_keepMineButton->setDefault(true);
    m_keepMineButton->setAutoDefault(true);
    m_keepMineButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #2d6cdf; color: #ffffff; border: none; "
        "border-radius: 4px; padding: 4px 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: #245bc0; }"));
    m_dismissButton->setFlat(true);
    m_dismissButton->setStyleSheet(QStringLiteral("QPushButton { color: #5a4a12; }"));

    // Compare is a G3-honest placeholder: present so the option is discoverable
    // once a diff view exists, but disabled with a tooltip that says WHY —
    // never a lying control that would silently do something else.
    m_compareButton->setEnabled(false);
    m_compareButton->setToolTip(
        tr("Comparing the two versions isn't available yet — a side-by-side "
           "diff view is not built. Use Reload to take the on-disk copy, or "
           "Keep mine to keep your version (Save then overwrites the file)."));

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
QString FileChangeBanner::reloadText() const { return m_reloadButton->text(); }
QString FileChangeBanner::keepMineText() const { return m_keepMineButton->text(); }
bool FileChangeBanner::keepMineIsDefault() const { return m_keepMineButton->isDefault(); }
bool FileChangeBanner::dismissIsFlat() const { return m_dismissButton->isFlat(); }

void FileChangeBanner::clickReloadForTest() { emit reloadRequested(); }
void FileChangeBanner::clickKeepMineForTest() { emit keepMineRequested(); }
void FileChangeBanner::clickSaveForTest() { emit saveRequested(); }
void FileChangeBanner::clickDismissForTest() {
    dismiss();
    emit dismissed();
}

} // namespace trailer
