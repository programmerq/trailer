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
    bool insertPagesFrom(const QString& sourcePath, int insertAtIndex);
    bool extractPages(const std::vector<int>& pageIndices, const QString& destPath) const;

    // Cropping. Margins are in PDF points, measured inward from each edge of the
    // current MediaBox. Returns false if the resulting rectangle would be invalid.
    bool cropPage(int pageIndex, double leftPts, double topPts,
                  double rightPts, double bottomPts);

    bool save(const QString& path);

    QPDF* qpdf() { return m_qpdf.get(); }

private:
    std::unique_ptr<QPDF> m_qpdf;
    std::vector<std::unique_ptr<QPDF>> m_sources;
    bool m_valid = false;
};

}  // namespace trailer
