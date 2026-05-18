#pragma once

#include <qpdf/QPDFObjectHandle.hh>

#include <QString>
#include <optional>
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

// Delete one or more pages by their indices in the page tree at
// apply() time. On the first apply(), the command captures a
// QPDFObjectHandle to each page so revert() can re-insert them at
// their original positions. qpdf retains the underlying page
// dictionary as long as something holds a handle to it, so the
// captured handles remain valid across the remove + re-insert
// cycle for the lifetime of the in-memory session.
//
// Re-applying after a revert re-removes by index. Indices captured
// at construction are stable across the apply→revert→apply round
// because revert restores the original positions.
class DeletePagesCommand : public PdfCommand {
  public:
    explicit DeletePagesCommand(std::vector<int> pageIndices);
    bool apply(PdfEditor &editor) override;
    bool revert(PdfEditor &editor) override;
    QString description() const override;

  private:
    // Sorted unique indices to delete, in ascending order.
    std::vector<int> m_indices;
    // Captured page handles, in the same order as m_indices. Empty
    // until the first apply().
    std::vector<QPDFObjectHandle> m_captured;
};

// Move the page at `from` to position `to`. The inverse is just
// movePage(currentPositionOfMovedPage, from); since movePage shifts
// every other page by one, the inverse `to`,`from` reproduces the
// pre-move ordering.
class MovePageCommand : public PdfCommand {
  public:
    MovePageCommand(int from, int to);
    bool apply(PdfEditor &editor) override;
    bool revert(PdfEditor &editor) override;
    QString description() const override;

  private:
    int m_from;
    int m_to;
};

// Insert all pages from `sourcePath` at `insertAtIndex`. The
// command captures the actual insert position and the number of
// pages inserted on the first successful apply(), so revert
// removes that exact contiguous range. Re-applying re-runs the
// insertion (the source file path is retained).
//
// apply() returns false if the source can't be opened, has no
// pages, or qpdf raises any other error during insertion. In that
// case the editor is not mutated and the PdfDocument layer must
// not push the command to the undo stack.
class InsertPagesCommand : public PdfCommand {
  public:
    InsertPagesCommand(QString sourcePath, int insertAtIndex);
    bool apply(PdfEditor &editor) override;
    bool revert(PdfEditor &editor) override;
    QString description() const override;

  private:
    QString m_sourcePath;
    int m_insertAtIndex;
    // Filled in on the first apply(). Used by both revert (to know
    // which contiguous range to drop) and a subsequent re-apply (to
    // produce the same insertion range even if the underlying file
    // changes between apply()s, which is a corner case we tolerate
    // by remembering the count from the original apply).
    int m_clampedIndex = -1;
    int m_insertedCount = 0;
};

// Crop one or more pages by setting their /CropBox to the rectangle
// derived from the supplied margins. The command captures each
// affected page's original /CropBox (which may be absent — in PDF
// the default is /MediaBox) so revert() restores the prior state
// exactly. A single user gesture that crops N pages produces ONE
// CropPageCommand instance with N captured entries, so undo reverts
// the whole batch atomically.
class CropPageCommand : public PdfCommand {
  public:
    CropPageCommand(std::vector<int> pageIndices, double leftPts, double topPts, double rightPts,
                    double bottomPts);
    bool apply(PdfEditor &editor) override;
    bool revert(PdfEditor &editor) override;
    QString description() const override;

  private:
    std::vector<int> m_indices;
    double m_left;
    double m_top;
    double m_right;
    double m_bottom;
    // Per-page original /CropBox, indexed parallel to m_indices.
    // std::nullopt means the page did not have a /CropBox at all
    // — revert removes the key rather than restoring an empty array.
    std::vector<std::optional<QPDFObjectHandle>> m_originalCropBoxes;
};

} // namespace trailer
