#pragma once

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

namespace trailer {

// Modal dialog that collects the four crop margins (in millimetres) for
// the Crop Pages command, plus an optional "apply to all pages" choice.
//
// The "Apply to all pages" checkbox is only offered when the document has
// more than one page. On a single-page document that choice is
// meaningless noise — there is nothing else to apply it to — so the
// checkbox is hidden and cropping targets the sole page (the same path
// the multi-page dialog takes when the box is left unchecked). This keeps
// the dialog honest about the choices it actually offers (PHILOSOPHY →
// *How Trailer reduces friction*; sibling of the Recognize-Text
// page-range friction). Ratified in
// docs/decision-records/0012-crop-single-page-apply-all-checkbox.md.
//
// Behaviour for a multi-page document is unchanged: the checkbox is shown,
// checked by default, and applyToAllPages() reflects it.
class CropPagesDialog : public QDialog {
    Q_OBJECT

  public:
    explicit CropPagesDialog(int pageCount, QWidget *parent = nullptr);

    // Entered margins, in millimetres.
    double leftMm() const;
    double topMm() const;
    double rightMm() const;
    double bottomMm() const;

    // True when the crop should apply to every page. Always false on a
    // single-page document (the checkbox is hidden), so the caller crops
    // the current page only.
    bool applyToAllPages() const;

    // Exposed for regression tests and G2 evidence grabs.
    QCheckBox *applyToAllCheckBox() const { return m_allPagesCheck; }

  private:
    // Cached at construction: a single-page document offers no
    // apply-to-all choice, so the checkbox is hidden and this stays false.
    bool m_multiPage = false;

    QDoubleSpinBox *m_left = nullptr;
    QDoubleSpinBox *m_top = nullptr;
    QDoubleSpinBox *m_right = nullptr;
    QDoubleSpinBox *m_bottom = nullptr;
    QCheckBox *m_allPagesCheck = nullptr;
};

} // namespace trailer
