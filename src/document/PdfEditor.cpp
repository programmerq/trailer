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

bool PdfEditor::cropPage(int pageIndex, double leftPts, double topPts,
                         double rightPts, double bottomPts) {
    if (!m_valid) return false;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
            return false;
        }
        QPDFPageObjectHelper page = pages[static_cast<size_t>(pageIndex)];
        QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
        if (!media.isArray() || media.getArrayNItems() < 4) {
            return false;
        }
        const double mx0 = media.getArrayItem(0).getNumericValue();
        const double my0 = media.getArrayItem(1).getNumericValue();
        const double mx1 = media.getArrayItem(2).getNumericValue();
        const double my1 = media.getArrayItem(3).getNumericValue();

        const double nx0 = mx0 + leftPts;
        const double ny0 = my0 + bottomPts;
        const double nx1 = mx1 - rightPts;
        const double ny1 = my1 - topPts;
        if (nx1 - nx0 < 1.0 || ny1 - ny0 < 1.0) {
            return false;
        }
        QPDFObjectHandle crop = QPDFObjectHandle::newArray(
            QPDFObjectHandle::Rectangle(nx0, ny0, nx1, ny1));
        page.getObjectHandle().replaceKey("/CropBox", crop);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool PdfEditor::extractPages(const std::vector<int>& pageIndices,
                             const QString& destPath) const {
    if (!m_valid || pageIndices.empty()) return false;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());

        QPDF dest;
        dest.emptyPDF();
        QPDFPageDocumentHelper destHelper(dest);

        for (int idx : pageIndices) {
            if (idx < 0 || idx >= total) continue;
            destHelper.addPage(pages[static_cast<size_t>(idx)], /*first=*/false);
        }

        QPDFWriter writer(dest, destPath.toLocal8Bit().constData());
        writer.setStaticID(false);
        writer.write();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

namespace {

QPDFObjectHandle colourArray(const QColor& c) {
    std::vector<QPDFObjectHandle> vals = {
        QPDFObjectHandle::newReal(static_cast<double>(c.redF()), 3),
        QPDFObjectHandle::newReal(static_cast<double>(c.greenF()), 3),
        QPDFObjectHandle::newReal(static_cast<double>(c.blueF()), 3),
    };
    return QPDFObjectHandle::newArray(vals);
}

QPDFObjectHandle rectArray(double x1, double y1, double x2, double y2) {
    return QPDFObjectHandle::newArray(
        QPDFObjectHandle::Rectangle(std::min(x1, x2), std::min(y1, y2),
                                    std::max(x1, x2), std::max(y1, y2)));
}

QPDFObjectHandle borderStyle(double width) {
    auto bs = QPDFObjectHandle::newDictionary();
    bs.replaceKey("/Type", QPDFObjectHandle::newName("/Border"));
    bs.replaceKey("/W", QPDFObjectHandle::newReal(width, 2));
    bs.replaceKey("/S", QPDFObjectHandle::newName("/S"));
    return bs;
}

QPDFObjectHandle buildAnnotation(const Annotation& a, double pageHeight) {
    auto dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    dict.replaceKey("/C", colourArray(a.style.stroke));
    dict.replaceKey("/BS", borderStyle(a.style.strokeWidth));
    if (!a.text.isEmpty()) {
        dict.replaceKey("/Contents",
            QPDFObjectHandle::newUnicodeString(a.text.toStdString()));
    }

    auto flipY = [pageHeight](double y) { return pageHeight - y; };
    const double x1 = a.bounds.left();
    const double y1top = a.bounds.top();
    const double x2 = a.bounds.right();
    const double y2bot = a.bounds.bottom();
    const double py1 = flipY(y2bot);
    const double py2 = flipY(y1top);

    switch (a.type) {
        case AnnotationType::Rectangle:
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Square"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
            break;
        case AnnotationType::Ellipse:
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Circle"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
            break;
        case AnnotationType::Line:
        case AnnotationType::Arrow: {
            if (a.points.size() < 2) return {};
            const double lx1 = a.points[0].x();
            const double ly1 = flipY(a.points[0].y());
            const double lx2 = a.points[1].x();
            const double ly2 = flipY(a.points[1].y());
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Line"));
            dict.replaceKey("/Rect", rectArray(lx1, ly1, lx2, ly2));
            std::vector<QPDFObjectHandle> L = {
                QPDFObjectHandle::newReal(lx1, 3),
                QPDFObjectHandle::newReal(ly1, 3),
                QPDFObjectHandle::newReal(lx2, 3),
                QPDFObjectHandle::newReal(ly2, 3),
            };
            dict.replaceKey("/L", QPDFObjectHandle::newArray(L));
            if (a.type == AnnotationType::Arrow) {
                std::vector<QPDFObjectHandle> le = {
                    QPDFObjectHandle::newName("/None"),
                    QPDFObjectHandle::newName("/OpenArrow"),
                };
                dict.replaceKey("/LE", QPDFObjectHandle::newArray(le));
            }
            break;
        }
        case AnnotationType::Ink: {
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Ink"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
            std::vector<QPDFObjectHandle> stroke;
            stroke.reserve(a.points.size() * 2);
            for (const QPointF& p : a.points) {
                stroke.push_back(QPDFObjectHandle::newReal(p.x(), 3));
                stroke.push_back(QPDFObjectHandle::newReal(flipY(p.y()), 3));
            }
            std::vector<QPDFObjectHandle> inkList = {
                QPDFObjectHandle::newArray(stroke),
            };
            dict.replaceKey("/InkList", QPDFObjectHandle::newArray(inkList));
            break;
        }
        case AnnotationType::Text: {
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/FreeText"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
            const int pt = a.style.fontPointSize > 0 ? a.style.fontPointSize : 12;
            const QString da = QStringLiteral("/Helv %1 Tf 0 0 0 rg").arg(pt);
            dict.replaceKey("/DA",
                QPDFObjectHandle::newString(da.toStdString()));
            break;
        }
        case AnnotationType::Note:
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x1 + 18.0, py1 + 18.0));
            dict.replaceKey("/Name", QPDFObjectHandle::newName("/Note"));
            break;
        case AnnotationType::HighlightShape: {
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Square"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
            QColor fill = a.style.fill.alpha() > 0 ? a.style.fill : a.style.stroke;
            dict.replaceKey("/IC", colourArray(fill));
            break;
        }
        case AnnotationType::SpeechBubble: {
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/FreeText"));
            dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
            const int pt = a.style.fontPointSize > 0 ? a.style.fontPointSize : 12;
            const QString da = QStringLiteral("/Helv %1 Tf 0 0 0 rg").arg(pt);
            dict.replaceKey("/DA",
                QPDFObjectHandle::newString(da.toStdString()));
            if (!a.points.empty()) {
                const QPointF tail = a.points.front();
                const double tx = tail.x();
                const double ty = flipY(tail.y());
                const double ax = x1 + (x2 - x1) * 0.25;
                const double ay = flipY(y2bot);
                std::vector<QPDFObjectHandle> cl = {
                    QPDFObjectHandle::newReal(tx, 3),
                    QPDFObjectHandle::newReal(ty, 3),
                    QPDFObjectHandle::newReal(ax, 3),
                    QPDFObjectHandle::newReal(ay, 3),
                };
                dict.replaceKey("/CL", QPDFObjectHandle::newArray(cl));
                std::vector<QPDFObjectHandle> le = {
                    QPDFObjectHandle::newName("/OpenArrow"),
                    QPDFObjectHandle::newName("/None"),
                };
                dict.replaceKey("/LE", QPDFObjectHandle::newArray(le));
            }
            break;
        }
        case AnnotationType::ZoomLens:
            // No standard PDF subtype for zoom-lens; persist only as image
            // flattening. Skip PDF serialisation (TODO: embed as /Stamp with
            // appearance stream).
            return {};
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut: {
            const char* subtype =
                a.type == AnnotationType::Highlight ? "/Highlight"
                : a.type == AnnotationType::Underline ? "/Underline"
                                                      : "/StrikeOut";
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName(subtype));
            std::vector<QRectF> rects = a.quads.empty()
                ? std::vector<QRectF>{a.bounds} : a.quads;
            std::vector<QPDFObjectHandle> qp;
            qp.reserve(rects.size() * 8);
            double rx1 = rects.front().left(), ry1 = flipY(rects.front().bottom());
            double rx2 = rects.front().right(), ry2 = flipY(rects.front().top());
            for (const QRectF& r : rects) {
                const double rl = r.left(), rr = r.right();
                const double rt = flipY(r.top()), rb = flipY(r.bottom());
                rx1 = std::min(rx1, rl); ry1 = std::min(ry1, rb);
                rx2 = std::max(rx2, rr); ry2 = std::max(ry2, rt);
                // QuadPoints: (x1,y1 x2,y2 x3,y3 x4,y4) = TL TR BL BR
                const double qs[8] = {rl, rt, rr, rt, rl, rb, rr, rb};
                for (double v : qs) qp.push_back(QPDFObjectHandle::newReal(v, 3));
            }
            dict.replaceKey("/QuadPoints", QPDFObjectHandle::newArray(qp));
            dict.replaceKey("/Rect", rectArray(rx1, ry1, rx2, ry2));
            break;
        }
    }
    return dict;
}

}  // namespace

bool PdfEditor::writeAnnotations(const std::vector<Annotation>& annotations) {
    if (!m_valid) return false;
    if (annotations.empty()) return true;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        for (int p = 0; p < total; ++p) {
            std::vector<QPDFObjectHandle> toAdd;
            QPDFPageObjectHelper& page = pages[static_cast<size_t>(p)];
            QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
            if (!media.isArray() || media.getArrayNItems() < 4) continue;
            const double my0 = media.getArrayItem(1).getNumericValue();
            const double my1 = media.getArrayItem(3).getNumericValue();
            const double pageHeight = my1 - my0;
            for (const Annotation& a : annotations) {
                if (a.page != p) continue;
                QPDFObjectHandle dict = buildAnnotation(a, pageHeight);
                if (dict.isDictionary()) {
                    toAdd.push_back(m_qpdf->makeIndirectObject(dict));
                }
            }
            if (toAdd.empty()) continue;
            QPDFObjectHandle pageObj = page.getObjectHandle();
            QPDFObjectHandle annots = pageObj.getKey("/Annots");
            if (!annots.isArray()) {
                annots = QPDFObjectHandle::newArray();
            }
            for (const auto& item : toAdd) {
                annots.appendItem(item);
            }
            pageObj.replaceKey("/Annots", annots);
        }
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
