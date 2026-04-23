#include "PdfEditor.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
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
    m_path = path;
    m_sources.clear();
    try {
        m_qpdf = std::make_unique<QPDF>();
        m_qpdf->processFile(path.toLocal8Bit().constData());
        m_valid = true;
        m_encrypted = false;
    } catch (const QPDFExc& e) {
        // qpdf reports password-gated PDFs with a specific error code.
        // Treat that as a recoverable state: stay loaded-but-locked,
        // caller can retry via unlock(). Everything else (corrupt
        // file, I/O error, etc.) is a hard failure.
        if (e.getErrorCode() == qpdf_e_password) {
            m_valid = false;
            m_encrypted = true;
        } else {
            m_valid = false;
            m_encrypted = false;
        }
    } catch (const std::exception&) {
        m_valid = false;
        m_encrypted = false;
    }
    return m_valid;
}

bool PdfEditor::unlock(const QString& password) {
    if (m_valid) return true;     // already unlocked
    if (!m_encrypted) return false;  // nothing to unlock
    try {
        m_qpdf = std::make_unique<QPDF>();
        m_qpdf->processFile(m_path.toLocal8Bit().constData(),
                            password.toUtf8().constData());
        m_valid = true;
        m_encrypted = false;
        return true;
    } catch (const std::exception&) {
        // Wrong password: stay locked, caller can retry.
        m_qpdf = std::make_unique<QPDF>();
        m_valid = false;
        return false;
    }
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

namespace {

QColor colourFromArray(QPDFObjectHandle arr) {
    if (!arr.isArray() || arr.getArrayNItems() < 3) return QColor();
    const double r = arr.getArrayItem(0).getNumericValue();
    const double g = arr.getArrayItem(1).getNumericValue();
    const double b = arr.getArrayItem(2).getNumericValue();
    QColor c;
    c.setRgbF(static_cast<float>(std::clamp(r, 0.0, 1.0)),
              static_cast<float>(std::clamp(g, 0.0, 1.0)),
              static_cast<float>(std::clamp(b, 0.0, 1.0)));
    return c;
}

QRectF rectFromArray(QPDFObjectHandle arr, double pageHeight) {
    if (!arr.isArray() || arr.getArrayNItems() < 4) return {};
    const double x1 = arr.getArrayItem(0).getNumericValue();
    const double y1 = arr.getArrayItem(1).getNumericValue();
    const double x2 = arr.getArrayItem(2).getNumericValue();
    const double y2 = arr.getArrayItem(3).getNumericValue();
    const double left = std::min(x1, x2);
    const double right = std::max(x1, x2);
    const double bottom = std::min(y1, y2);
    const double top = std::max(y1, y2);
    // Flip back to top-left origin.
    return QRectF(QPointF(left, pageHeight - top),
                  QPointF(right, pageHeight - bottom));
}

}  // namespace

std::vector<Annotation> PdfEditor::readAnnotations() const {
    std::vector<Annotation> out;
    if (!m_valid) return out;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        for (int p = 0; p < total; ++p) {
            QPDFPageObjectHelper& page = pages[static_cast<size_t>(p)];
            QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
            if (!media.isArray() || media.getArrayNItems() < 4) continue;
            const double my0 = media.getArrayItem(1).getNumericValue();
            const double my1 = media.getArrayItem(3).getNumericValue();
            const double pageHeight = my1 - my0;
            auto flipY = [pageHeight](double y) { return pageHeight - y; };

            QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
            if (!annots.isArray()) continue;
            const int n = annots.getArrayNItems();
            for (int i = 0; i < n; ++i) {
                QPDFObjectHandle entry = annots.getArrayItem(i);
                if (!entry.isDictionary()) continue;
                QPDFObjectHandle subtype = entry.getKey("/Subtype");
                if (!subtype.isName()) continue;
                const std::string st = subtype.getName();

                Annotation a;
                a.page = p;
                a.bounds = rectFromArray(entry.getKey("/Rect"), pageHeight);
                QColor stroke = colourFromArray(entry.getKey("/C"));
                if (stroke.isValid()) a.style.stroke = stroke;
                QPDFObjectHandle bs = entry.getKey("/BS");
                if (bs.isDictionary()) {
                    QPDFObjectHandle w = bs.getKey("/W");
                    if (w.isNumber()) a.style.strokeWidth = w.getNumericValue();
                }
                QPDFObjectHandle contents = entry.getKey("/Contents");
                if (contents.isString()) {
                    a.text = QString::fromStdString(contents.getUTF8Value());
                }

                if (st == "/Square") {
                    QPDFObjectHandle ic = entry.getKey("/IC");
                    if (ic.isArray()) {
                        QColor fill = colourFromArray(ic);
                        if (fill.isValid()) {
                            a.style.fill = fill;
                            a.type = AnnotationType::HighlightShape;
                        } else {
                            a.type = AnnotationType::Rectangle;
                        }
                    } else {
                        a.type = AnnotationType::Rectangle;
                    }
                } else if (st == "/Circle") {
                    a.type = AnnotationType::Ellipse;
                } else if (st == "/Line") {
                    QPDFObjectHandle L = entry.getKey("/L");
                    if (!L.isArray() || L.getArrayNItems() < 4) continue;
                    const double x1 = L.getArrayItem(0).getNumericValue();
                    const double y1 = L.getArrayItem(1).getNumericValue();
                    const double x2 = L.getArrayItem(2).getNumericValue();
                    const double y2 = L.getArrayItem(3).getNumericValue();
                    a.points = {QPointF(x1, flipY(y1)), QPointF(x2, flipY(y2))};
                    QPDFObjectHandle le = entry.getKey("/LE");
                    bool isArrow = false;
                    if (le.isArray() && le.getArrayNItems() >= 2) {
                        QPDFObjectHandle end = le.getArrayItem(1);
                        if (end.isName() && end.getName() != "/None") isArrow = true;
                    }
                    a.type = isArrow ? AnnotationType::Arrow
                                     : AnnotationType::Line;
                } else if (st == "/Ink") {
                    QPDFObjectHandle inkList = entry.getKey("/InkList");
                    if (!inkList.isArray() || inkList.getArrayNItems() < 1) continue;
                    QPDFObjectHandle stroke = inkList.getArrayItem(0);
                    if (!stroke.isArray()) continue;
                    const int sn = stroke.getArrayNItems();
                    for (int k = 0; k + 1 < sn; k += 2) {
                        const double x = stroke.getArrayItem(k).getNumericValue();
                        const double y = stroke.getArrayItem(k + 1).getNumericValue();
                        a.points.emplace_back(x, flipY(y));
                    }
                    if (a.points.size() < 2) continue;
                    a.type = AnnotationType::Ink;
                } else if (st == "/FreeText") {
                    QPDFObjectHandle cl = entry.getKey("/CL");
                    if (cl.isArray() && cl.getArrayNItems() >= 2) {
                        const double tx = cl.getArrayItem(0).getNumericValue();
                        const double ty = cl.getArrayItem(1).getNumericValue();
                        a.points = {QPointF(tx, flipY(ty))};
                        a.type = AnnotationType::SpeechBubble;
                    } else {
                        a.type = AnnotationType::Text;
                    }
                } else if (st == "/Text") {
                    a.type = AnnotationType::Note;
                } else if (st == "/Highlight" || st == "/Underline"
                           || st == "/StrikeOut") {
                    a.type = st == "/Highlight" ? AnnotationType::Highlight
                           : st == "/Underline" ? AnnotationType::Underline
                                                : AnnotationType::StrikeOut;
                    QPDFObjectHandle qp = entry.getKey("/QuadPoints");
                    if (qp.isArray()) {
                        const int qn = qp.getArrayNItems();
                        for (int k = 0; k + 7 < qn; k += 8) {
                            const double x1 = qp.getArrayItem(k).getNumericValue();
                            const double y1 = qp.getArrayItem(k + 1).getNumericValue();
                            const double x2 = qp.getArrayItem(k + 2).getNumericValue();
                            // (quad points: TL TR BL BR in PDF Y-up)
                            const double y3 = qp.getArrayItem(k + 5).getNumericValue();
                            a.quads.emplace_back(
                                QPointF(std::min(x1, x2), flipY(y1)),
                                QPointF(std::max(x1, x2), flipY(y3)));
                        }
                    }
                    if (a.quads.empty()) a.quads.push_back(a.bounds);
                } else {
                    continue;
                }
                out.push_back(std::move(a));
            }
        }
    } catch (const std::exception&) {
        return out;
    }
    return out;
}

// ---------------------------------------------------------------------------
// AcroForm field access (Phase 5)
// ---------------------------------------------------------------------------

bool PdfEditor::hasFormFields() const {
    if (!m_valid) return false;
    try {
        QPDFAcroFormDocumentHelper afdh(*m_qpdf);
        return !afdh.getFormFields().empty();
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<FormField> PdfEditor::readFormFields() const {
    std::vector<FormField> result;
    if (!m_valid) return result;
    try {
        QPDFAcroFormDocumentHelper afdh(*m_qpdf);
        QPDFPageDocumentHelper pdh(*m_qpdf);
        auto pages = pdh.getAllPages();  // non-const — getAnnotations() is non-const
        const int pageCount = static_cast<int>(pages.size());

        // Build objId → page-index so we can locate each widget's page cheaply.
        std::map<int, int> annotToPage;
        for (int pi = 0; pi < pageCount; ++pi) {
            for (auto& annot : pages[static_cast<size_t>(pi)].getAnnotations()) {
                annotToPage[annot.getObjectHandle().getObjGen().getObj()] = pi;
            }
        }

        int id = 0;
        for (auto& field : afdh.getFormFields()) {
            // Skip push-buttons — they have no user-editable value.
            if (field.isPushbutton()) { ++id; continue; }

            FormField ff;
            ff.id = id++;
            ff.name  = QString::fromStdString(field.getFullyQualifiedName());
            ff.label = QString::fromStdString(field.getAlternativeName());

            if (field.isText()) {
                ff.type = FormFieldType::Text;
                const int flags = field.getFlags();
                ff.multiline  = (flags & ff_tx_multiline)  != 0;
                ff.isPassword = (flags & ff_tx_password)   != 0;
            } else if (field.isCheckbox()) {
                ff.type = FormFieldType::Checkbox;
            } else if (field.isRadioButton()) {
                ff.type = FormFieldType::RadioButton;
            } else if (field.isChoice()) {
                ff.type = FormFieldType::Dropdown;
                for (const auto& opt : field.getChoices())
                    ff.options << QString::fromStdString(opt);
            } else {
                ff.type = FormFieldType::Unknown;
            }

            // Value
            if (field.isCheckbox() || field.isRadioButton()) {
                ff.value = field.isChecked()
                    ? QStringLiteral("Yes") : QStringLiteral("Off");
            } else {
                ff.value = QString::fromStdString(field.getValueAsString());
            }

            // Common flags
            const int flags = field.getFlags();
            ff.readOnly = (flags & ff_all_read_only) != 0;
            ff.required = (flags & ff_all_required)  != 0;

            // Rect and page from the first widget annotation.
            auto annots = afdh.getAnnotationsForField(field);
            if (!annots.empty()) {
                const auto r = annots.front().getRect();  // non-const method
                ff.rectPts = QRectF(r.llx, r.lly,
                                    r.urx - r.llx, r.ury - r.lly);
                const int oid =
                    annots.front().getObjectHandle().getObjGen().getObj();
                const auto it = annotToPage.find(oid);
                if (it != annotToPage.end()) ff.page = it->second;
            }

            result.push_back(std::move(ff));
        }
    } catch (const std::exception&) {}
    return result;
}

bool PdfEditor::setFormFieldValue(int id, const QString& value) {
    if (!m_valid) return false;
    try {
        QPDFAcroFormDocumentHelper afdh(*m_qpdf);
        auto fields = afdh.getFormFields();
        // id maps to the same positional walk used by readFormFields(),
        // skipping push-buttons.
        int idx = 0;
        for (auto& field : fields) {
            if (field.isPushbutton()) { ++idx; continue; }
            if (idx == id) {
                if (field.getFlags() & ff_all_read_only) return false;
                field.setV(value.toStdString(), /*need_appearances=*/true);
                afdh.setNeedAppearances(true);
                return true;
            }
            ++idx;
        }
        return false;  // id out of range
    } catch (const std::exception&) {
        return false;
    }
}

// ---------------------------------------------------------------------------

bool PdfEditor::save(const QString& path) {
    return saveImpl(path, nullptr);
}

bool PdfEditor::save(const QString& path, const EncryptionOptions& enc) {
    return saveImpl(path, &enc);
}

bool PdfEditor::saveImpl(const QString& path, const EncryptionOptions* enc) {
    if (!m_valid) return false;
    try {
        QPDFWriter writer(*m_qpdf, path.toLocal8Bit().constData());
        writer.setStaticID(false);
        if (enc) {
            // qpdf expects both passwords as C strings. An empty user
            // password is the well-defined "anyone can open but
            // permissions still apply" case.
            const QByteArray user = enc->userPassword.toUtf8();
            const QByteArray owner = enc->ownerPassword.isEmpty()
                                         ? user
                                         : enc->ownerPassword.toUtf8();
            const qpdf_r3_print_e printLevel =
                !enc->allowPrint
                    ? qpdf_r3p_none
                    : (enc->allowHighResPrint ? qpdf_r3p_full : qpdf_r3p_low);
            // Use R6 (AES-256) — the only password scheme that's still
            // considered secure and the only one the current PDF spec
            // sanctions. Older Rx methods are available on qpdf via
            // *Insecure-suffixed functions; we deliberately don't
            // expose them.
            writer.setR6EncryptionParameters(
                user.constData(), owner.constData(),
                enc->allowAccessibility,
                enc->allowExtract,
                enc->allowModify,          // allow_assemble
                enc->allowAnnotate,
                enc->allowFormFilling,
                enc->allowModify,          // allow_modify_other
                printLevel,
                /*encrypt_metadata_aes=*/true);
        }
        writer.write();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace trailer
