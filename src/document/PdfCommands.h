#pragma once

#include <QString>
#include <memory>
#include <vector>

namespace trailer {

class PdfEditor;

// Base class for an undo-able PDF mutation. Each concrete command
// captures enough state on construction to both apply its forward
// effect and revert it later. The PdfDocument keeps two stacks of
// owning unique_ptrs — a command moves between them as the user
// undoes / redoes.
//
// This is the qpdf-level counterpart to AnnotationStore's existing
// undo log. Annotations and qpdf mutations live in separate stacks
// today; the unified chronological ordering is captured as a
// follow-up in TODO.md.
class PdfCommand {
  public:
    virtual ~PdfCommand() = default;
    // Forward effect. Returns true on success; the document only
    // pushes the command to the undo stack when this returns true.
    virtual bool apply(PdfEditor &editor) = 0;
    // Inverse of apply. Should restore the editor to the state it
    // was in before apply() ran. Idempotent — calling apply then
    // revert then apply again must produce the same result as a
    // single apply.
    virtual bool revert(PdfEditor &editor) = 0;
    // Short verb-phrase description, used by the Edit menu's
    // "Undo X" / "Redo X" labels and by debug logs.
    virtual QString description() const = 0;
};

// Rotate `pageIndex` by `degreesClockwise` (must be a multiple of
// 90; PDF /Rotate is integer-valued in 90° steps). The inverse is
// the same call with `-degreesClockwise`.
class RotatePageCommand : public PdfCommand {
  public:
    RotatePageCommand(int pageIndex, int degreesClockwise);
    bool apply(PdfEditor &editor) override;
    bool revert(PdfEditor &editor) override;
    QString description() const override;

  private:
    int m_pageIndex;
    int m_degrees;
};

} // namespace trailer
