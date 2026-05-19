#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QRadioButton;
class QSpinBox;

namespace trailer {

// Parameter-supply dialog for Tools → Recognize Text… Replaces the
// previous "dump extracted text into a QPlainTextEdit" results dialog
// (Workstream F): OCR results now feed the in-document
// SelectableTextStore so the user reads and selects them in-place.
//
// The user supplies:
//   - Pages: Current / All / Range (with from/to spin boxes).
//   - Language: combobox auto-detect + supported variants. Row is
//     hidden when only English is present (the current ship state).
//   - Force re-run: checkbox. Defaults off. Enables a re-OCR even
//     when hasTextLayer() returns true — for PDFs whose "text layer"
//     is a corner watermark and not the actual page content.
//
// On accept, the caller submits work via OcrController; the dialog
// itself does not run OCR.
class RecognizeTextDialog : public QDialog {
    Q_OBJECT
  public:
    enum class PageScope { Current, All, Range };

    // Construct the dialog. `pageCount` is the document's page count
    // (used to populate the spin-box ranges); `currentPage` seeds the
    // default selection. `hasTextLayer` controls whether the
    // "Force re-run even if a text layer exists" checkbox is shown
    // (hidden for pure-raster docs that don't have a layer to begin
    // with — there's nothing to force past).
    RecognizeTextDialog(int pageCount, int currentPage, bool hasTextLayer,
                        const QStringList &languageOptions, QWidget *parent = nullptr);

    PageScope scope() const;
    // Returns the resolved 0-based page indices the user picked.
    // `Current` -> [currentPage]; `All` -> [0..pageCount); `Range`
    // -> [from..to] (inclusive). Empty when the dialog was rejected.
    std::vector<int> resolvedPages() const;

    // The user-selected language option's data string ("auto",
    // "en", ...). Empty when only one option is available (the row
    // was hidden).
    QString language() const;

    bool forceRerun() const;

  private slots:
    void onScopeChanged();

  private:
    int m_pageCount;
    int m_currentPage;

    QButtonGroup *m_scopeGroup = nullptr;
    QRadioButton *m_currentRadio = nullptr;
    QRadioButton *m_allRadio = nullptr;
    QRadioButton *m_rangeRadio = nullptr;
    QSpinBox *m_fromSpin = nullptr;
    QSpinBox *m_toSpin = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QCheckBox *m_forceRerunCheck = nullptr;
};

} // namespace trailer
