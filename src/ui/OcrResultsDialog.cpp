#include "OcrResultsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

namespace trailer {

OcrResultsDialog::OcrResultsDialog(
    const QString& sourceName,
    const QVector<OcrEngine::TextBlock>& blocks,
    QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Recognized Text"));
    resize(640, 480);

    auto* layout = new QVBoxLayout(this);

    m_summary = new QLabel(this);
    if (blocks.isEmpty()) {
        m_summary->setText(tr("No text was detected in this image."));
    } else {
        m_summary->setText(
            tr("Found %n text region(s). Review, copy, or export below.",
               nullptr, static_cast<int>(blocks.size())));
    }
    layout->addWidget(m_summary);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(false);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QString body;
    for (const auto& b : blocks) {
        body += b.text;
        body += QLatin1Char('\n');
    }
    m_text->setPlainText(body);
    layout->addWidget(m_text, /*stretch=*/1);

    auto* buttons = new QDialogButtonBox(this);
    auto* copyBtn = buttons->addButton(tr("Copy All"),
                                       QDialogButtonBox::ActionRole);
    auto* saveBtn = buttons->addButton(tr("Save as TXT…"),
                                       QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(copyBtn, &QPushButton::clicked, this, &OcrResultsDialog::onCopy);
    connect(saveBtn, &QPushButton::clicked, this, &OcrResultsDialog::onSaveAs);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);

    m_suggestedBaseName = QFileInfo(sourceName).completeBaseName();
    if (m_suggestedBaseName.isEmpty()) {
        m_suggestedBaseName = QStringLiteral("recognized-text");
    }
}

QString OcrResultsDialog::plainText() const {
    return m_text ? m_text->toPlainText() : QString();
}

void OcrResultsDialog::onCopy() {
    QApplication::clipboard()->setText(plainText());
}

void OcrResultsDialog::onSaveAs() {
    const QString suggested =
        m_suggestedBaseName + QStringLiteral(".txt");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Recognized Text"),
        suggested, tr("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Could not open %1 for writing.").arg(path));
        return;
    }
    QTextStream out(&f);
    out << plainText();
}

}  // namespace trailer
