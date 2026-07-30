#include "FeedbackDialog.h"

#include "app/Application.h"
#include "diagnostics/FeedbackReport.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace trailer {

class FeedbackDialog::Impl {
  public:
    feedback::AppSnapshot snapshot;
};

FeedbackDialog::FeedbackDialog(Application *app, QWidget *parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>()) {
    // Gathered once at construction — the checkbox toggle below just
    // re-renders the same snapshot with/without paths, it never
    // re-queries live app state. That keeps the report describing a
    // single, consistent instant regardless of how long the dialog
    // stays open.
    m_impl->snapshot = feedback::gatherSnapshot(app);

    setWindowTitle(tr("Feedback Report"));
    resize(720, 640);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("This report describes Trailer's current state: version, platform, open "
           "documents, view modes, and settings. It stays on your device — nothing is "
           "sent anywhere. Read it over, then copy it into a GitHub issue or hand it to "
           "a coding agent yourself."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_text, /*stretch=*/1);

    m_pathsCheck = new QCheckBox(
        tr("Include full file paths (may reveal folder names or a username)"), this);
    m_pathsCheck->setChecked(false); // privacy default — see FeedbackReport.h
    layout->addWidget(m_pathsCheck);
    connect(m_pathsCheck, &QCheckBox::toggled, this, &FeedbackDialog::refresh);

    auto *buttonRow = new QDialogButtonBox(this);
    m_copyButton =
        buttonRow->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
    buttonRow->addButton(QDialogButtonBox::Close);
    connect(m_copyButton, &QPushButton::clicked, this,
            [this]() { QGuiApplication::clipboard()->setText(m_text->toPlainText()); });
    connect(buttonRow, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonRow);

    refresh();
}

FeedbackDialog::~FeedbackDialog() = default;

QString FeedbackDialog::reportText() const {
    return m_text ? m_text->toPlainText() : QString();
}

void FeedbackDialog::refresh() {
    m_text->setPlainText(
        feedback::formatMarkdown(m_impl->snapshot, m_pathsCheck->isChecked()));
}

void showFeedbackReportDialog(QWidget *parent, Application *app) {
    FeedbackDialog dialog(app, parent);
    dialog.exec();
}

} // namespace trailer
