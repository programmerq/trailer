#include "PdfCommands.h"

#include "PdfEditor.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <QObject>

#include <algorithm>
#include <set>
#include <utility>

namespace trailer {

// ---- Rotate ---------------------------------------------------------------

RotatePageCommand::RotatePageCommand(int pageIndex, int degreesClockwise)
    : m_pageIndex(pageIndex), m_degrees(degreesClockwise) {}

bool RotatePageCommand::apply(PdfEditor &editor) {
    // PdfEditor::rotatePage is void — it tolerates out-of-range
    // indices by silently doing nothing. The PdfDocument layer
    // pre-validates so we just call through and report success.
    editor.rotatePage(m_pageIndex, m_degrees);
    return true;
}

bool RotatePageCommand::revert(PdfEditor &editor) {
    editor.rotatePage(m_pageIndex, -m_degrees);
    return true;
}

QString RotatePageCommand::description() const {
    return QObject::tr("Rotate Page");
}

// ---- Delete ---------------------------------------------------------------

DeletePagesCommand::DeletePagesCommand(std::vector<int> pageIndices) {
    // Normalise: dedupe and sort ascending. Storing in ascending
    // order makes revert(N) trivially correct — we re-insert in the
    // same order so each `addPageAt(refIndex == i)` places the page
    // back at exactly its original position because earlier
    // re-inserts already filled in positions 0..i-1.
    std::set<int> unique(pageIndices.begin(), pageIndices.end());
    m_indices.assign(unique.begin(), unique.end());
}

bool DeletePagesCommand::apply(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (m_indices.empty())
        return false;
    try {
        QPDFPageDocumentHelper helper(*pdf);
        auto pages = helper.getAllPages();
        const int total = static_cast<int>(pages.size());
        if (static_cast<int>(m_indices.size()) >= total)
            return false; // refuse to delete every page
        for (int idx : m_indices) {
            if (idx < 0 || idx >= total)
                return false; // any bad index aborts the whole delete
        }

        // Capture page handles (object-handle copies) BEFORE removal
        // so revert can re-insert. qpdf reference-counts page objects
        // through QPDFObjectHandle; the handle survives removal from
        // the page tree.
        if (m_captured.empty()) {
            m_captured.reserve(m_indices.size());
            for (int idx : m_indices) {
                m_captured.push_back(pages[static_cast<size_t>(idx)].getObjectHandle());
            }
        }

        // Remove from highest to lowest so each removal doesn't
        // shift the indices of pages we haven't yet processed.
        for (auto it = m_indices.rbegin(); it != m_indices.rend(); ++it) {
            helper.removePage(pages[static_cast<size_t>(*it)]);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool DeletePagesCommand::revert(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (m_captured.empty())
        return false;
    try {
        QPDFPageDocumentHelper helper(*pdf);
        // Re-insert in ascending order. For each captured page, place
        // it before the page currently at its original index; if its
        // original index equals the current page count, append at end.
        for (size_t i = 0; i < m_indices.size(); ++i) {
            const int origIndex = m_indices[i];
            QPDFPageObjectHelper page(m_captured[i]);
            auto current = helper.getAllPages();
            if (origIndex >= static_cast<int>(current.size())) {
                helper.addPage(page, /*first=*/false);
            } else {
                helper.addPageAt(page, /*before=*/true, current[static_cast<size_t>(origIndex)]);
            }
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

QString DeletePagesCommand::description() const {
    return m_indices.size() > 1U ? QObject::tr("Delete Pages") : QObject::tr("Delete Page");
}

// ---- Move -----------------------------------------------------------------

MovePageCommand::MovePageCommand(int from, int to) : m_from(from), m_to(to) {}

bool MovePageCommand::apply(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (m_from == m_to)
        return false;
    const int total = editor.pageCount();
    if (m_from < 0 || m_from >= total || m_to < 0 || m_to >= total)
        return false;

    // Capture the moved page's handle on the FIRST apply only so
    // revert can re-insert it at the original index by handle,
    // independent of where movePage's internals leave it. (See the
    // header comment for why we can't just call move(to, from).)
    // A subsequent apply (redo after revert) re-uses the same handle
    // — qpdf reference-counts page objects through QPDFObjectHandle,
    // so the captured handle survives the remove+re-insert cycle.
    if (!m_movedPage.has_value()) {
        try {
            auto pages = QPDFPageDocumentHelper(*pdf).getAllPages();
            m_movedPage = pages[static_cast<size_t>(m_from)].getObjectHandle();
        } catch (const std::exception &) {
            return false;
        }
    }

    editor.movePage(m_from, m_to);
    return true;
}

bool MovePageCommand::revert(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (!m_movedPage.has_value())
        return false;

    try {
        QPDFPageDocumentHelper helper(*pdf);
        // Remove the moved page by handle (qpdf locates it in the
        // page tree regardless of its current position).
        QPDFPageObjectHelper page(*m_movedPage);
        helper.removePage(page);

        // Re-insert at the original `from`. Now that the page is
        // gone, the remaining list has the OTHER pages in their
        // pre-apply relative order. Insert before whatever's
        // currently at index m_from (which is the page that was
        // originally at m_from+1) — that puts the moved page back
        // at index m_from. If m_from was the very last index pre-
        // apply, the remaining list has m_from entries and we
        // append at the end instead.
        auto remaining = helper.getAllPages();
        if (m_from < static_cast<int>(remaining.size())) {
            helper.addPageAt(page, /*before=*/true,
                             remaining[static_cast<size_t>(m_from)]);
        } else {
            helper.addPage(page, /*first=*/false);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

QString MovePageCommand::description() const {
    return QObject::tr("Move Page");
}

// ---- Insert ---------------------------------------------------------------

InsertPagesCommand::InsertPagesCommand(QString sourcePath, int insertAtIndex)
    : m_sourcePath(std::move(sourcePath)), m_insertAtIndex(insertAtIndex) {}

bool InsertPagesCommand::apply(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    const int before = editor.pageCount();
    if (!editor.insertPagesFrom(m_sourcePath, m_insertAtIndex))
        return false;
    const int after = editor.pageCount();
    if (after <= before) {
        // The editor returned true but no pages were actually
        // inserted — treat as a soft failure so the command isn't
        // pushed onto the undo stack with no work to revert.
        return false;
    }
    const int actualInserted = after - before;
    const int clamped = std::clamp(m_insertAtIndex, 0, before);
    if (m_insertedCount == 0) {
        // First apply — snapshot the actual position + count so
        // revert removes the correct contiguous range.
        m_insertedCount = actualInserted;
        m_clampedIndex = clamped;
        return true;
    }
    // Re-apply (redo after undo). If the source file's page count
    // has changed since the first apply, the inserted range no
    // longer matches the snapshot — revert would either leave
    // extra pages behind or remove too many. Roll back the just-
    // inserted range and refuse, so the undo stack stays consistent
    // and the caller doesn't push the broken command.
    if (actualInserted != m_insertedCount || clamped != m_clampedIndex) {
        try {
            QPDFPageDocumentHelper helper(*pdf);
            auto pages = helper.getAllPages();
            const int total = static_cast<int>(pages.size());
            const int rollbackStart = clamped;
            const int rollbackEnd =
                std::min(rollbackStart + actualInserted, total);
            for (int i = rollbackEnd - 1; i >= rollbackStart && i >= 0; --i) {
                helper.removePage(pages[static_cast<size_t>(i)]);
            }
        } catch (const std::exception &) {
            // Best effort. If the rollback throws, the editor is in
            // an inconsistent state, but we still refuse the redo so
            // the undo stack isn't lying about being able to revert.
        }
        return false;
    }
    return true;
}

bool InsertPagesCommand::revert(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (m_insertedCount <= 0 || m_clampedIndex < 0)
        return false;
    try {
        QPDFPageDocumentHelper helper(*pdf);
        auto pages = helper.getAllPages();
        const int total = static_cast<int>(pages.size());
        if (m_clampedIndex + m_insertedCount > total)
            return false; // someone else mutated the doc; abort
        // Walk the inserted range from the bottom up so removals
        // don't shift the indices of pages still queued for removal.
        for (int i = m_clampedIndex + m_insertedCount - 1; i >= m_clampedIndex; --i) {
            helper.removePage(pages[static_cast<size_t>(i)]);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

QString InsertPagesCommand::description() const {
    return QObject::tr("Insert Pages");
}

// ---- Crop -----------------------------------------------------------------

CropPageCommand::CropPageCommand(std::vector<int> pageIndices, double leftPts, double topPts,
                                 double rightPts, double bottomPts)
    : m_left(leftPts), m_top(topPts), m_right(rightPts), m_bottom(bottomPts) {
    // Dedupe + sort. Order doesn't strictly matter for crop, but
    // keeps revert deterministic and matches DeletePagesCommand's
    // convention.
    std::set<int> unique(pageIndices.begin(), pageIndices.end());
    m_indices.assign(unique.begin(), unique.end());
}

bool CropPageCommand::apply(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (m_indices.empty())
        return false;

    try {
        QPDFPageDocumentHelper helper(*pdf);
        auto pages = helper.getAllPages();
        const int total = static_cast<int>(pages.size());

        const bool firstApply = m_originalCropBoxes.empty();
        if (firstApply) {
            m_originalCropBoxes.reserve(m_indices.size());
        }

        bool any = false;
        for (size_t i = 0; i < m_indices.size(); ++i) {
            const int idx = m_indices[i];
            if (idx < 0 || idx >= total) {
                if (firstApply)
                    m_originalCropBoxes.emplace_back(std::nullopt);
                continue;
            }
            // Snapshot the current /CropBox (or nullopt if none) on
            // the first apply only. Subsequent applies (after
            // revert) reuse the original snapshot so revert
            // continues to restore the pre-very-first state.
            QPDFObjectHandle pageObj = pages[static_cast<size_t>(idx)].getObjectHandle();
            if (firstApply) {
                if (pageObj.hasKey("/CropBox")) {
                    m_originalCropBoxes.emplace_back(pageObj.getKey("/CropBox").shallowCopy());
                } else {
                    m_originalCropBoxes.emplace_back(std::nullopt);
                }
            }
            if (editor.cropPage(idx, m_left, m_top, m_right, m_bottom)) {
                any = true;
            }
        }
        return any;
    } catch (const std::exception &) {
        return false;
    }
}

bool CropPageCommand::revert(PdfEditor &editor) {
    QPDF *pdf = editor.qpdf();
    if (!editor.isValid() || !pdf)
        return false;
    if (m_originalCropBoxes.empty())
        return false;
    try {
        auto pages = QPDFPageDocumentHelper(*pdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        // m_originalCropBoxes is keyed parallel to m_indices. A page
        // that wasn't cropped on the first apply (out-of-range or
        // oversized margins) has a nullopt slot but we still walked
        // past it. Walk m_indices here and restore only where the
        // page is still in range.
        for (size_t i = 0; i < m_indices.size(); ++i) {
            const int idx = m_indices[i];
            if (idx < 0 || idx >= total)
                continue;
            QPDFObjectHandle pageObj = pages[static_cast<size_t>(idx)].getObjectHandle();
            const auto &orig = m_originalCropBoxes[i];
            if (orig.has_value()) {
                pageObj.replaceKey("/CropBox", *orig);
            } else {
                // No prior /CropBox; remove the one apply() added so
                // the page falls back to its /MediaBox default.
                pageObj.removeKey("/CropBox");
            }
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

QString CropPageCommand::description() const {
    return m_indices.size() > 1U ? QObject::tr("Crop Pages") : QObject::tr("Crop Page");
}

} // namespace trailer
