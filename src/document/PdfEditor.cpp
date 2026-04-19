#include "PdfEditor.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <set>

namespace trailer {

PdfEditor::PdfEditor() : m_qpdf(std::make_unique<QPDF>()) {}

PdfEditor::~PdfEditor() = default;

bool PdfEditor::load(const QString& path) {
    try {
        m_qpdf = std::make_unique<QPDF>();
        m_qpdf->processFile(path.toLocal8Bit().constData());
        m_sources.clear();
        m_valid = true;
    } catch (const std::exception&) {
        m_valid = false;
    }
    return m_valid;
}

int PdfEditor::pageCount() const {
    if (!m_valid) return 0;
    try {
        return static_cast<int>(
            QPDFPageDocumentHelper(*m_qpdf).getAllPages().size());
    } catch (const std::exception&) {
        return 0;
    }
}

void PdfEditor::rotatePage(int pageIndex, int degreesClockwise) {
    if (!m_valid) return;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
            return;
        }
        pages[static_cast<size_t>(pageIndex)].rotatePage(degreesClockwise,
                                                         /*relative=*/true);
    } catch (const std::exception&) {
    }
}

void PdfEditor::deletePages(std::vector<int> pageIndices) {
    if (!m_valid) return;
    try {
        QPDFPageDocumentHelper helper(*m_qpdf);
        auto pages = helper.getAllPages();
        const int total = static_cast<int>(pages.size());

        std::set<int> unique(pageIndices.begin(), pageIndices.end());
        std::vector<int> sorted(unique.rbegin(), unique.rend());
        for (int idx : sorted) {
            if (idx < 0 || idx >= total) continue;
            helper.removePage(pages[static_cast<size_t>(idx)]);
        }
    } catch (const std::exception&) {
    }
}

void PdfEditor::movePage(int from, int to) {
    if (!m_valid) return;
    try {
        QPDFPageDocumentHelper helper(*m_qpdf);
        auto pages = helper.getAllPages();
        const int total = static_cast<int>(pages.size());
        if (from < 0 || from >= total || to < 0 || to >= total || from == to) {
            return;
        }
        QPDFPageObjectHelper page = pages[static_cast<size_t>(from)];
        helper.removePage(page);

        auto remaining = helper.getAllPages();
        const int adjusted = (from < to) ? (to - 1) : to;
        if (adjusted >= static_cast<int>(remaining.size())) {
            helper.addPage(page, /*first=*/false);
        } else {
            helper.addPageAt(page, /*before=*/true,
                             remaining[static_cast<size_t>(adjusted)]);
        }
    } catch (const std::exception&) {
    }
}

bool PdfEditor::insertPagesFrom(const QString& sourcePath, int insertAtIndex) {
    if (!m_valid) return false;
    try {
        auto source = std::make_unique<QPDF>();
        source->processFile(sourcePath.toLocal8Bit().constData());

        QPDFPageDocumentHelper destHelper(*m_qpdf);
        QPDFPageDocumentHelper sourceHelper(*source);
        auto srcPages = sourceHelper.getAllPages();
        if (srcPages.empty()) {
            return false;
        }

        auto destPages = destHelper.getAllPages();
        const int destCount = static_cast<int>(destPages.size());
        const int clamped = std::clamp(insertAtIndex, 0, destCount);

        if (clamped >= destCount) {
            for (auto& p : srcPages) {
                destHelper.addPage(p, /*first=*/false);
            }
        } else {
            QPDFPageObjectHelper refPage = destPages[static_cast<size_t>(clamped)];
            for (auto& p : srcPages) {
                destHelper.addPageAt(p, /*before=*/true, refPage);
            }
        }

        m_sources.push_back(std::move(source));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool PdfEditor::save(const QString& path) {
    if (!m_valid) return false;
    try {
        QPDFWriter writer(*m_qpdf, path.toLocal8Bit().constData());
        writer.setStaticID(false);
        writer.write();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace trailer
