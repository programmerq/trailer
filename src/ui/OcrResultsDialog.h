#pragma once

#include "ml/OcrEngine.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QPlainTextEdit;
class QLabel;

namespace trailer {

// Minimal "Recognize Text" results dialog. Shows the extracted text
// in a copyable QPlainTextEdit and offers Copy-all + Save-as-TXT
// affordances. DESIGN §6.11.2 envisions a richer Live Text overlay
// with per-word selection and in-place searchable-PDF embedding; for
// Phase 6D we stick to the simple text-extraction path which is the
// 90% use case (scan a receipt, pull the numbers, paste them).
class OcrResultsDialog : public QDialog {
    Q_OBJECT
  public:
    OcrResultsDialog(const QString &sourceName, const QVector<OcrEngine::TextBlock> &blocks,
                     QWidget *parent = nullptr);

    // Joined plain text — one TextBlock per line, in reading order.
    QString plainText() const;

  private slots:
    void onCopy();
    void onSaveAs();

  private:
    QPlainTextEdit *m_text = nullptr;
    QLabel *m_summary = nullptr;
    QString m_suggestedBaseName;
};

} // namespace trailer
