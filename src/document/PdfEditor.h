#pragma once

#include <QString>
#include <memory>
#include <vector>

class QPDF;

namespace trailer {

class PdfEditor {
public:
    PdfEditor();
    ~PdfEditor();

    PdfEditor(const PdfEditor&) = delete;
    PdfEditor& operator=(const PdfEditor&) = delete;

    bool load(const QString& path);
    bool isValid() const { return m_valid; }
    int pageCount() const;

    void rotatePage(int pageIndex, int degreesClockwise);
    void deletePages(std::vector<int> pageIndices);
    void movePage(int from, int to);

    bool save(const QString& path);

    QPDF* qpdf() { return m_qpdf.get(); }

private:
    std::unique_ptr<QPDF> m_qpdf;
    bool m_valid = false;
};

}  // namespace trailer
