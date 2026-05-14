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

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QTemporaryFile>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace trailer {

PdfEditor::PdfEditor() : m_qpdf(std::make_unique<QPDF>()) {}

PdfEditor::~PdfEditor() = default;

bool PdfEditor::load(const QString &path) {
    m_path = path;
    m_sources.clear();
    try {
        m_qpdf = std::make_unique<QPDF>();
        m_qpdf->processFile(path.toLocal8Bit().constData());
        m_valid = true;
        m_encrypted = false;
    } catch (const QPDFExc &e) {
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
    } catch (const std::exception &) {
        m_valid = false;
        m_encrypted = false;
    }
    return m_valid;
}

bool PdfEditor::unlock(const QString &password) {
    if (m_valid)
        return true; // already unlocked
    if (!m_encrypted)
        return false; // nothing to unlock
    try {
        m_qpdf = std::make_unique<QPDF>();
        m_qpdf->processFile(m_path.toLocal8Bit().constData(), password.toUtf8().constData());
        m_valid = true;
        m_encrypted = false;
        return true;
    } catch (const std::exception &) {
        // Wrong password: stay locked, caller can retry.
        m_qpdf = std::make_unique<QPDF>();
        m_valid = false;
        return false;
    }
}

int PdfEditor::pageCount() const {
    if (!m_valid)
        return 0;
    try {
        return static_cast<int>(QPDFPageDocumentHelper(*m_qpdf).getAllPages().size());
    } catch (const std::exception &) {
        return 0;
    }
}

void PdfEditor::rotatePage(int pageIndex, int degreesClockwise) {
    if (!m_valid)
        return;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
            return;
        }
        pages[static_cast<size_t>(pageIndex)].rotatePage(degreesClockwise,
                                                         /*relative=*/true);
    } catch (const std::exception &) {
    }
}

void PdfEditor::deletePages(std::vector<int> pageIndices) {
    if (!m_valid)
        return;
    try {
        QPDFPageDocumentHelper helper(*m_qpdf);
        auto pages = helper.getAllPages();
        const int total = static_cast<int>(pages.size());

        std::set<int> unique(pageIndices.begin(), pageIndices.end());
        std::vector<int> sorted(unique.rbegin(), unique.rend());
        for (int idx : sorted) {
            if (idx < 0 || idx >= total)
                continue;
            helper.removePage(pages[static_cast<size_t>(idx)]);
        }
    } catch (const std::exception &) {
    }
}

void PdfEditor::movePage(int from, int to) {
    if (!m_valid)
        return;
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
            helper.addPageAt(page, /*before=*/true, remaining[static_cast<size_t>(adjusted)]);
        }
    } catch (const std::exception &) {
    }
}

bool PdfEditor::insertPagesFrom(const QString &sourcePath, int insertAtIndex) {
    if (!m_valid)
        return false;
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
            for (auto &p : srcPages) {
                destHelper.addPage(p, /*first=*/false);
            }
        } else {
            QPDFPageObjectHelper refPage = destPages[static_cast<size_t>(clamped)];
            for (auto &p : srcPages) {
                destHelper.addPageAt(p, /*before=*/true, refPage);
            }
        }

        m_sources.push_back(std::move(source));
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool PdfEditor::cropPage(int pageIndex, double leftPts, double topPts, double rightPts,
                         double bottomPts) {
    if (!m_valid)
        return false;
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
        QPDFObjectHandle crop =
            QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(nx0, ny0, nx1, ny1));
        page.getObjectHandle().replaceKey("/CropBox", crop);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool PdfEditor::extractPages(const std::vector<int> &pageIndices, const QString &destPath) const {
    if (!m_valid || pageIndices.empty())
        return false;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());

        QPDF dest;
        dest.emptyPDF();
        QPDFPageDocumentHelper destHelper(dest);

        for (int idx : pageIndices) {
            if (idx < 0 || idx >= total)
                continue;
            destHelper.addPage(pages[static_cast<size_t>(idx)], /*first=*/false);
        }

        QPDFWriter writer(dest, destPath.toLocal8Bit().constData());
        writer.setStaticID(false);
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

namespace {

QPDFObjectHandle colourArray(const QColor &c) {
    std::vector<QPDFObjectHandle> vals = {
        QPDFObjectHandle::newReal(static_cast<double>(c.redF()), 3),
        QPDFObjectHandle::newReal(static_cast<double>(c.greenF()), 3),
        QPDFObjectHandle::newReal(static_cast<double>(c.blueF()), 3),
    };
    return QPDFObjectHandle::newArray(vals);
}

QPDFObjectHandle rectArray(double x1, double y1, double x2, double y2) {
    return QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(
        std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2)));
}

QPDFObjectHandle borderStyle(double width) {
    auto bs = QPDFObjectHandle::newDictionary();
    bs.replaceKey("/Type", QPDFObjectHandle::newName("/Border"));
    bs.replaceKey("/W", QPDFObjectHandle::newReal(width, 2));
    bs.replaceKey("/S", QPDFObjectHandle::newName("/S"));
    return bs;
}

// Build a Form XObject content stream that draws `a` as a stroke
// (and optional fill) rectangle inside its /Rect bounds. Used for
// Rectangle and HighlightShape annotations so external viewers
// like Apple Preview render the shape from /AP rather than falling
// back to a property-only render that may show blank.
//
// PDF coords are bottom-left origin; the caller passes already-
// flipped (px1, py1)-(px2, py2). The XObject's /BBox uses the same
// coords with /Matrix = identity so the stream draws absolutely.
QPDFObjectHandle buildSquareAppearance(QPDF &pdf, const Annotation &a, double px1, double py1,
                                       double px2, double py2) {
    const QColor &stroke = a.style.stroke;
    const QColor &fill = a.style.fill;
    const double sw = a.style.strokeWidth > 0.0 ? a.style.strokeWidth : 1.0;

    // Inset by half the stroke width so the stroked outline fits
    // entirely inside /Rect without clipping.
    const double inset = sw / 2.0;
    const double rx = px1 + inset;
    const double ry = py1 + inset;
    const double rw = (px2 - px1) - sw;
    const double rh = (py2 - py1) - sw;

    char buf[512];
    size_t n = 0;
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "q\n"));
    n += static_cast<size_t>(std::snprintf(
        buf + n, sizeof(buf) - n, "%.3f %.3f %.3f RG\n", static_cast<double>(stroke.redF()),
        static_cast<double>(stroke.greenF()), static_cast<double>(stroke.blueF())));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "%.3f w\n", sw));
    if (fill.isValid() && fill.alpha() > 0) {
        n += static_cast<size_t>(std::snprintf(
            buf + n, sizeof(buf) - n, "%.3f %.3f %.3f rg\n", static_cast<double>(fill.redF()),
            static_cast<double>(fill.greenF()), static_cast<double>(fill.blueF())));
        n += static_cast<size_t>(
            std::snprintf(buf + n, sizeof(buf) - n, "%.3f %.3f %.3f %.3f re B\n", rx, ry, rw, rh));
    } else {
        n += static_cast<size_t>(
            std::snprintf(buf + n, sizeof(buf) - n, "%.3f %.3f %.3f %.3f re S\n", rx, ry, rw, rh));
    }
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "Q\n"));

    auto dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/FormType", QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/Resources", QPDFObjectHandle::newDictionary());

    auto bbox = QPDFObjectHandle::newArray();
    bbox.appendItem(QPDFObjectHandle::newReal(px1, 3));
    bbox.appendItem(QPDFObjectHandle::newReal(py1, 3));
    bbox.appendItem(QPDFObjectHandle::newReal(px2, 3));
    bbox.appendItem(QPDFObjectHandle::newReal(py2, 3));
    dict.replaceKey("/BBox", bbox);

    auto stream = QPDFObjectHandle::newStream(&pdf, std::string(buf, buf + n));
    stream.replaceDict(dict);
    return stream;
}

// Wrap an XObject in the standard /AP /N dictionary used by an
// /Annot to point at its appearance stream.
QPDFObjectHandle wrapAppearance(QPDFObjectHandle xobj) {
    auto ap = QPDFObjectHandle::newDictionary();
    ap.replaceKey("/N", xobj);
    return ap;
}

// Helpers shared by the type-specific appearance builders below.
namespace ap {

// Build the boilerplate Form XObject dict around a content stream.
QPDFObjectHandle finishStream(QPDF &pdf, const char *buf, size_t len, double px1, double py1,
                              double px2, double py2) {
    auto dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/FormType", QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/Resources", QPDFObjectHandle::newDictionary());

    auto bbox = QPDFObjectHandle::newArray();
    bbox.appendItem(QPDFObjectHandle::newReal(px1, 3));
    bbox.appendItem(QPDFObjectHandle::newReal(py1, 3));
    bbox.appendItem(QPDFObjectHandle::newReal(px2, 3));
    bbox.appendItem(QPDFObjectHandle::newReal(py2, 3));
    dict.replaceKey("/BBox", bbox);

    auto stream = QPDFObjectHandle::newStream(&pdf, std::string(buf, buf + len));
    stream.replaceDict(dict);
    return stream;
}

} // namespace ap

// Build a Form XObject for an /Circle (ellipse) annotation. PDF
// has no native ellipse operator; ISO 32000 prescribes four cubic
// Bezier arcs with control offsets at kappa * radius. The kappa
// value 0.5522847... is the canonical magic number for a circle.
// For an ellipse we use kappa per axis independently.
QPDFObjectHandle buildCircleAppearance(QPDF &pdf, const Annotation &a, double px1, double py1,
                                       double px2, double py2) {
    const QColor &stroke = a.style.stroke;
    const QColor &fill = a.style.fill;
    const double sw = a.style.strokeWidth > 0.0 ? a.style.strokeWidth : 1.0;

    const double inset = sw / 2.0;
    const double cx = (px1 + px2) * 0.5;
    const double cy = (py1 + py2) * 0.5;
    const double rx = (px2 - px1) * 0.5 - inset;
    const double ry = (py2 - py1) * 0.5 - inset;
    constexpr double kKappa = 0.5522847498307933;
    const double ox = rx * kKappa;
    const double oy = ry * kKappa;

    char buf[1024];
    size_t n = 0;
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "q\n"));
    n += static_cast<size_t>(std::snprintf(
        buf + n, sizeof(buf) - n, "%.3f %.3f %.3f RG\n", static_cast<double>(stroke.redF()),
        static_cast<double>(stroke.greenF()), static_cast<double>(stroke.blueF())));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "%.3f w\n", sw));
    if (fill.isValid() && fill.alpha() > 0) {
        n += static_cast<size_t>(std::snprintf(
            buf + n, sizeof(buf) - n, "%.3f %.3f %.3f rg\n", static_cast<double>(fill.redF()),
            static_cast<double>(fill.greenF()), static_cast<double>(fill.blueF())));
    }
    // Start at the right of the ellipse, run counter-clockwise.
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "%.3f %.3f m\n", cx + rx, cy));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n,
                                           "%.3f %.3f %.3f %.3f %.3f %.3f c\n", cx + rx, cy + oy,
                                           cx + ox, cy + ry, cx, cy + ry));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n,
                                           "%.3f %.3f %.3f %.3f %.3f %.3f c\n", cx - ox, cy + ry,
                                           cx - rx, cy + oy, cx - rx, cy));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n,
                                           "%.3f %.3f %.3f %.3f %.3f %.3f c\n", cx - rx, cy - oy,
                                           cx - ox, cy - ry, cx, cy - ry));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n,
                                           "%.3f %.3f %.3f %.3f %.3f %.3f c\n", cx + ox, cy - ry,
                                           cx + rx, cy - oy, cx + rx, cy));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "h %s\nQ\n",
                                           (fill.isValid() && fill.alpha() > 0) ? "B" : "S"));
    return ap::finishStream(pdf, buf, n, px1, py1, px2, py2);
}

// Build a Form XObject for a /Line annotation. Optional arrowhead
// at the end (not on the start) when `arrow` is true. The arrow is
// a simple two-segment open-V centred on the line's terminal angle.
QPDFObjectHandle buildLineAppearance(QPDF &pdf, const Annotation &a, bool arrow, double lx1,
                                     double ly1, double lx2, double ly2) {
    const QColor &stroke = a.style.stroke;
    const double sw = a.style.strokeWidth > 0.0 ? a.style.strokeWidth : 1.0;

    char buf[1024];
    size_t n = 0;
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "q\n"));
    n += static_cast<size_t>(std::snprintf(
        buf + n, sizeof(buf) - n, "%.3f %.3f %.3f RG\n", static_cast<double>(stroke.redF()),
        static_cast<double>(stroke.greenF()), static_cast<double>(stroke.blueF())));
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "%.3f w\n", sw));
    n += static_cast<size_t>(
        std::snprintf(buf + n, sizeof(buf) - n, "%.3f %.3f m %.3f %.3f l S\n", lx1, ly1, lx2, ly2));
    if (arrow) {
        // Arrowhead: two short segments back from the terminal point
        // at ±25° from the line direction. Length scales with stroke
        // width so a thicker line gets a chunkier head.
        const double dx = lx2 - lx1;
        const double dy = ly2 - ly1;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.0001) {
            const double ux = dx / len;
            const double uy = dy / len;
            const double headLen = std::max(8.0, sw * 4.0);
            constexpr double kCos = 0.9063078; // cos(25°)
            constexpr double kSin = 0.4226183; // sin(25°)
            const double rx = -ux * kCos + uy * kSin;
            const double ry = -uy * kCos - ux * kSin;
            const double lhx = lx2 + rx * headLen;
            const double lhy = ly2 + ry * headLen;
            const double rxv = -ux * kCos - uy * kSin;
            const double ryv = -uy * kCos + ux * kSin;
            const double rhx = lx2 + rxv * headLen;
            const double rhy = ly2 + ryv * headLen;
            n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n,
                                                   "%.3f %.3f m %.3f %.3f l %.3f %.3f l S\n", lhx,
                                                   lhy, lx2, ly2, rhx, rhy));
        }
    }
    n += static_cast<size_t>(std::snprintf(buf + n, sizeof(buf) - n, "Q\n"));

    // BBox sized to enclose both endpoints with a stroke-width pad.
    const double pad = std::max(sw * 4.0, 12.0);
    const double bx1 = std::min(lx1, lx2) - pad;
    const double by1 = std::min(ly1, ly2) - pad;
    const double bx2 = std::max(lx1, lx2) + pad;
    const double by2 = std::max(ly1, ly2) + pad;
    return ap::finishStream(pdf, buf, n, bx1, by1, bx2, by2);
}

// Build a Form XObject for an /Ink annotation. Walks the polyline
// emitting `m`/`l` operators; PDF readers without custom Ink
// rendering still get a stroked line through the user's path. When
// per-sample pressures are present, each segment is stroked
// independently with its own line width — the cubic curve maps
// pressure to a [base, base+5] width range so a stylus or Force
// Touch trackpad stroke reads as variably-weighted ink.
QPDFObjectHandle buildInkAppearance(QPDF &pdf, const Annotation &a,
                                    const std::vector<QPointF> &flippedPts, double px1, double py1,
                                    double px2, double py2) {
    if (flippedPts.size() < 2)
        return {};
    const QColor &stroke = a.style.stroke;
    const double sw = a.style.strokeWidth > 0.0 ? a.style.strokeWidth : 1.0;
    const bool withPressure = !a.pressures.empty() && a.pressures.size() == flippedPts.size();

    constexpr int kBufSize = 32 * 1024;
    const size_t cap = static_cast<size_t>(kBufSize);
    auto *buf = new char[kBufSize];
    size_t n = 0;
    n += static_cast<size_t>(std::snprintf(buf + n, cap - n, "q\n"));
    n += static_cast<size_t>(
        std::snprintf(buf + n, cap - n, "%.3f %.3f %.3f RG\n", static_cast<double>(stroke.redF()),
                      static_cast<double>(stroke.greenF()), static_cast<double>(stroke.blueF())));
    n += static_cast<size_t>(std::snprintf(buf + n, cap - n, "1 J 1 j\n"));
    if (!withPressure) {
        n += static_cast<size_t>(std::snprintf(buf + n, cap - n, "%.3f w\n", sw));
        n += static_cast<size_t>(
            std::snprintf(buf + n, cap - n, "%.3f %.3f m\n", flippedPts[0].x(), flippedPts[0].y()));
        for (size_t i = 1; i < flippedPts.size() && n < cap - 64; ++i) {
            n += static_cast<size_t>(std::snprintf(buf + n, cap - n, "%.3f %.3f l\n",
                                                   flippedPts[i].x(), flippedPts[i].y()));
        }
        n += static_cast<size_t>(std::snprintf(buf + n, cap - n, "S\n"));
    } else {
        for (size_t i = 1; i < flippedPts.size() && n < cap - 128; ++i) {
            const double pr = static_cast<double>(std::clamp(a.pressures[i], 0.0f, 1.0f));
            const double shaped = pr * pr * pr;
            const double w = sw + shaped * 5.0;
            n += static_cast<size_t>(std::snprintf(
                buf + n, cap - n, "%.3f w %.3f %.3f m %.3f %.3f l S\n", w, flippedPts[i - 1].x(),
                flippedPts[i - 1].y(), flippedPts[i].x(), flippedPts[i].y()));
        }
    }
    n += static_cast<size_t>(std::snprintf(buf + n, cap - n, "Q\n"));
    QPDFObjectHandle out = ap::finishStream(pdf, buf, n, px1, py1, px2, py2);
    delete[] buf;
    return out;
}

QPDFObjectHandle buildAnnotation(QPDF &pdf, const Annotation &a, double pageHeight) {
    auto dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    dict.replaceKey("/C", colourArray(a.style.stroke));
    dict.replaceKey("/BS", borderStyle(a.style.strokeWidth));
    if (!a.text.isEmpty()) {
        dict.replaceKey("/Contents", QPDFObjectHandle::newUnicodeString(a.text.toStdString()));
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
        // /AP appearance stream so external viewers (Apple
        // Preview, Acrobat in some configs) render the rectangle
        // even when they don't reconstruct from /C and /BS.
        dict.replaceKey("/AP", wrapAppearance(pdf.makeIndirectObject(
                                   buildSquareAppearance(pdf, a, x1, py1, x2, py2))));
        break;
    case AnnotationType::Ellipse:
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Circle"));
        dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
        dict.replaceKey("/AP", wrapAppearance(pdf.makeIndirectObject(
                                   buildCircleAppearance(pdf, a, x1, py1, x2, py2))));
        break;
    case AnnotationType::Line:
    case AnnotationType::Arrow: {
        if (a.points.size() < 2)
            return {};
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
        dict.replaceKey("/AP", wrapAppearance(pdf.makeIndirectObject(buildLineAppearance(
                                   pdf, a, a.type == AnnotationType::Arrow, lx1, ly1, lx2, ly2))));
        break;
    }
    case AnnotationType::Ink: {
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Ink"));
        dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
        std::vector<QPDFObjectHandle> stroke;
        stroke.reserve(a.points.size() * 2);
        std::vector<QPointF> flippedPts;
        flippedPts.reserve(a.points.size());
        for (const QPointF &p : a.points) {
            const double pyf = flipY(p.y());
            stroke.push_back(QPDFObjectHandle::newReal(p.x(), 3));
            stroke.push_back(QPDFObjectHandle::newReal(pyf, 3));
            flippedPts.emplace_back(p.x(), pyf);
        }
        std::vector<QPDFObjectHandle> inkList = {
            QPDFObjectHandle::newArray(stroke),
        };
        dict.replaceKey("/InkList", QPDFObjectHandle::newArray(inkList));
        QPDFObjectHandle inkAp = buildInkAppearance(pdf, a, flippedPts, x1, py1, x2, py2);
        if (inkAp.isStream()) {
            dict.replaceKey("/AP", wrapAppearance(pdf.makeIndirectObject(inkAp)));
        }
        break;
    }
    case AnnotationType::Text: {
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/FreeText"));
        dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
        const int pt = a.style.fontPointSize > 0 ? a.style.fontPointSize : 12;
        const QString da = QStringLiteral("/Helv %1 Tf 0 0 0 rg").arg(pt);
        dict.replaceKey("/DA", QPDFObjectHandle::newString(da.toStdString()));
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
        // /AP with the explicit fill colour; HighlightShape
        // would otherwise rely on the reader respecting /IC,
        // which Apple Preview doesn't always.
        Annotation withFill = a;
        withFill.style.fill = fill;
        dict.replaceKey("/AP", wrapAppearance(pdf.makeIndirectObject(
                                   buildSquareAppearance(pdf, withFill, x1, py1, x2, py2))));
        break;
    }
    case AnnotationType::SpeechBubble: {
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/FreeText"));
        dict.replaceKey("/Rect", rectArray(x1, py1, x2, py2));
        const int pt = a.style.fontPointSize > 0 ? a.style.fontPointSize : 12;
        const QString da = QStringLiteral("/Helv %1 Tf 0 0 0 rg").arg(pt);
        dict.replaceKey("/DA", QPDFObjectHandle::newString(da.toStdString()));
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
    case AnnotationType::Signature:
        // Signatures are flattened into the page's content stream via
        // flattenSignatures(); not stored as /Annot objects. Skipping
        // here keeps writeAnnotations() oblivious to the image stamp.
        return {};
    case AnnotationType::Redaction:
        // Redactions are destroyed in applyRedactions() by replacing
        // the page's content stream with a rasterised image.
        // They must never reappear as /Annot entries — if they did,
        // the "redacted" content would still be present on the page.
        return {};
    case AnnotationType::Highlight:
    case AnnotationType::Underline:
    case AnnotationType::StrikeOut: {
        const char *subtype = a.type == AnnotationType::Highlight   ? "/Highlight"
                              : a.type == AnnotationType::Underline ? "/Underline"
                                                                    : "/StrikeOut";
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName(subtype));
        std::vector<QRectF> rects = a.quads.empty() ? std::vector<QRectF>{a.bounds} : a.quads;
        std::vector<QPDFObjectHandle> qp;
        qp.reserve(rects.size() * 8);
        double rx1 = rects.front().left(), ry1 = flipY(rects.front().bottom());
        double rx2 = rects.front().right(), ry2 = flipY(rects.front().top());
        for (const QRectF &r : rects) {
            const double rl = r.left(), rr = r.right();
            const double rt = flipY(r.top()), rb = flipY(r.bottom());
            rx1 = std::min(rx1, rl);
            ry1 = std::min(ry1, rb);
            rx2 = std::max(rx2, rr);
            ry2 = std::max(ry2, rt);
            // QuadPoints: (x1,y1 x2,y2 x3,y3 x4,y4) = TL TR BL BR
            const double qs[8] = {rl, rt, rr, rt, rl, rb, rr, rb};
            for (double v : qs)
                qp.push_back(QPDFObjectHandle::newReal(v, 3));
        }
        dict.replaceKey("/QuadPoints", QPDFObjectHandle::newArray(qp));
        dict.replaceKey("/Rect", rectArray(rx1, ry1, rx2, ry2));
        break;
    }
    }
    return dict;
}

} // namespace

namespace {

// Encode a QImage as two FlateDecode-ready raw byte buffers: one for the
// RGB colour plane, one for the 8-bit alpha SMask. The alpha buffer is
// empty when the input is fully opaque — callers then omit the SMask.
struct EncodedSignature {
    std::string rgb;
    std::string alpha;
    int width = 0;
    int height = 0;
};

EncodedSignature encodeSignatureImage(const QImage &src) {
    EncodedSignature out;
    if (src.isNull())
        return out;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    out.width = w;
    out.height = h;
    out.rgb.reserve(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);
    out.alpha.reserve(static_cast<size_t>(w) * static_cast<size_t>(h));
    bool anyNonOpaque = false;
    for (int y = 0; y < h; ++y) {
        const auto *scan = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = scan[x];
            out.rgb.push_back(static_cast<char>(qRed(px)));
            out.rgb.push_back(static_cast<char>(qGreen(px)));
            out.rgb.push_back(static_cast<char>(qBlue(px)));
            const int a = qAlpha(px);
            if (a != 255)
                anyNonOpaque = true;
            out.alpha.push_back(static_cast<char>(a));
        }
    }
    if (!anyNonOpaque)
        out.alpha.clear();
    return out;
}

// Create an indirect /XObject Image from a QImage. The caller owns the
// lifetime (it's stored in the page resources). Raw pixel data is
// handed to qpdf uncompressed — QPDFWriter compresses it with
// /FlateDecode on save by default.
QPDFObjectHandle makeSignatureXObject(QPDF &pdf, const QImage &src) {
    EncodedSignature enc = encodeSignatureImage(src);
    if (enc.width <= 0 || enc.height <= 0)
        return {};

    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf, enc.rgb);
    QPDFObjectHandle d = stream.getDict();
    d.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    d.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    d.replaceKey("/Width", QPDFObjectHandle::newInteger(enc.width));
    d.replaceKey("/Height", QPDFObjectHandle::newInteger(enc.height));
    d.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
    d.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));

    if (!enc.alpha.empty()) {
        QPDFObjectHandle smask = QPDFObjectHandle::newStream(&pdf, enc.alpha);
        QPDFObjectHandle sd = smask.getDict();
        sd.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
        sd.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
        sd.replaceKey("/Width", QPDFObjectHandle::newInteger(enc.width));
        sd.replaceKey("/Height", QPDFObjectHandle::newInteger(enc.height));
        sd.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceGray"));
        sd.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
        d.replaceKey("/SMask", pdf.makeIndirectObject(smask));
    }
    return pdf.makeIndirectObject(stream);
}

} // namespace

bool PdfEditor::flattenSignatures(const std::vector<Annotation> &annotations) {
    if (!m_valid)
        return false;
    // Collect signatures per page so we only touch each page's
    // resources and content stream once.
    std::map<int, std::vector<const Annotation *>> byPage;
    for (const Annotation &a : annotations) {
        if (a.type != AnnotationType::Signature)
            continue;
        if (a.imagePath.isEmpty())
            continue;
        byPage[a.page].push_back(&a);
    }
    if (byPage.empty())
        return true;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        // Share one XObject across multiple placements of the same PNG.
        std::map<QString, QPDFObjectHandle> xobjByPath;
        int seq = 0;
        for (auto &kv : byPage) {
            const int pageIdx = kv.first;
            if (pageIdx < 0 || pageIdx >= total)
                continue;
            QPDFPageObjectHelper &page = pages[static_cast<size_t>(pageIdx)];
            QPDFObjectHandle pageObj = page.getObjectHandle();
            QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
            if (!media.isArray() || media.getArrayNItems() < 4)
                continue;
            const double my0 = media.getArrayItem(1).getNumericValue();
            const double my1 = media.getArrayItem(3).getNumericValue();
            const double pageHeight = my1 - my0;

            QPDFObjectHandle resources = page.getAttribute("/Resources", /*copy_if_shared=*/true);
            if (!resources.isDictionary()) {
                resources = QPDFObjectHandle::newDictionary();
            }
            QPDFObjectHandle xobjDict = resources.getKey("/XObject");
            if (!xobjDict.isDictionary()) {
                xobjDict = QPDFObjectHandle::newDictionary();
            }

            std::string draw;
            for (const Annotation *a : kv.second) {
                auto it = xobjByPath.find(a->imagePath);
                QPDFObjectHandle xobj;
                if (it == xobjByPath.end()) {
                    QImage img(a->imagePath);
                    if (img.isNull())
                        continue;
                    xobj = makeSignatureXObject(*m_qpdf, img);
                    if (!xobj.isInitialized())
                        continue;
                    xobjByPath[a->imagePath] = xobj;
                } else {
                    xobj = it->second;
                }
                const std::string resName = "/TrailerSig" + std::to_string(++seq);
                xobjDict.replaceKey(resName, xobj);

                // PDF coords: origin bottom-left. Our bounds use top-
                // left origin so flip Y. The image transform `cm` is
                // `sx 0 0 sy tx ty` to scale from 1×1 unit-space to the
                // actual width/height and translate to (x, y_bottom).
                const double w = a->bounds.width();
                const double h = a->bounds.height();
                const double x = a->bounds.left();
                const double yTop = a->bounds.top();
                const double y = pageHeight - yTop - h;

                char buf[192];
                std::snprintf(buf, sizeof(buf), "\nq %.3f 0 0 %.3f %.3f %.3f cm %s Do Q", w, h, x,
                              y, resName.c_str());
                draw += buf;
            }

            if (draw.empty())
                continue;

            resources.replaceKey("/XObject", xobjDict);
            pageObj.replaceKey("/Resources", resources);

            // Wrap the existing content with q/Q so our appended draw
            // commands start from a clean graphics state. The page may
            // have a single stream or an array; addPageContents handles
            // both, and adds the new stream as a sibling rather than
            // mutating the existing one.
            QPDFObjectHandle prepend =
                QPDFObjectHandle::newStream(m_qpdf.get(), std::string("q\n"));
            QPDFObjectHandle append =
                QPDFObjectHandle::newStream(m_qpdf.get(), std::string("Q\n") + draw + "\n");
            page.addPageContents(prepend, /*first=*/true);
            page.addPageContents(append, /*first=*/false);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool PdfEditor::applyRedactions(const std::vector<Annotation> &annotations) {
    if (!m_valid)
        return false;
    std::map<int, std::vector<const Annotation *>> byPage;
    for (const Annotation &a : annotations) {
        if (a.type == AnnotationType::Redaction)
            byPage[a.page].push_back(&a);
    }
    if (byPage.empty())
        return true;

    // Write the current editor state to a temp file so QPdfDocument can
    // render it. Qt's QPdfDocument is file-backed; it doesn't accept
    // qpdf's in-memory graph directly. Using a QTemporaryFile with
    // RAII cleanup is safer than managing a path by hand.
    QTemporaryFile snapshot(QDir::tempPath() + QStringLiteral("/trailer-redact-XXXXXX.pdf"));
    snapshot.setAutoRemove(true);
    if (!snapshot.open())
        return false;
    const QString snapshotPath = snapshot.fileName();
    snapshot.close();
    try {
        QPDFWriter writer(*m_qpdf, snapshotPath.toLocal8Bit().constData());
        writer.setStaticID(false);
        writer.write();
    } catch (const std::exception &) {
        return false;
    }

    QPdfDocument doc;
    if (doc.load(snapshotPath) != QPdfDocument::Error::None)
        return false;

    // Render at 200 DPI so redacted pages still look crisp at 100%
    // zoom on a retina display. Higher values bloat file size; 200 is
    // a good default for body text.
    constexpr double kDpi = 200.0;
    constexpr double kPtsPerInch = 72.0;
    const double scale = kDpi / kPtsPerInch;

    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        for (const auto &kv : byPage) {
            const int idx = kv.first;
            if (idx < 0 || idx >= total)
                continue;
            QPDFPageObjectHelper &page = pages[static_cast<size_t>(idx)];
            QPDFObjectHandle pageObj = page.getObjectHandle();

            QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
            if (!media.isArray() || media.getArrayNItems() < 4)
                continue;
            const double mx0 = media.getArrayItem(0).getNumericValue();
            const double my0 = media.getArrayItem(1).getNumericValue();
            const double mx1 = media.getArrayItem(2).getNumericValue();
            const double my1 = media.getArrayItem(3).getNumericValue();
            const double widthPts = mx1 - mx0;
            const double heightPts = my1 - my0;
            if (widthPts <= 0.0 || heightPts <= 0.0)
                continue;

            const QSize pixelSize(std::max(1, static_cast<int>(widthPts * scale)),
                                  std::max(1, static_cast<int>(heightPts * scale)));
            QImage raster = doc.render(idx, pixelSize);
            if (raster.isNull())
                continue;
            // Force an alpha channel so the image XObject's SMask stays
            // consistent with the signature path — the format is cheap
            // to produce and harmless when fully opaque.
            raster = raster.convertToFormat(QImage::Format_ARGB32);

            // Paint opaque black over each redaction. Input bounds are
            // in doc-native points with top-left origin — raster
            // pixels match the same orientation, so no Y flip needed.
            {
                QPainter p(&raster);
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::black);
                for (const Annotation *a : kv.second) {
                    const QRectF r(a->bounds.left() * scale, a->bounds.top() * scale,
                                   a->bounds.width() * scale, a->bounds.height() * scale);
                    p.drawRect(r);
                }
                p.end();
            }

            QPDFObjectHandle xobj = makeSignatureXObject(*m_qpdf, raster);
            if (!xobj.isInitialized())
                continue;

            QPDFObjectHandle resources = page.getAttribute("/Resources", /*copy_if_shared=*/true);
            if (!resources.isDictionary()) {
                resources = QPDFObjectHandle::newDictionary();
            }
            QPDFObjectHandle xobjDict = resources.getKey("/XObject");
            if (!xobjDict.isDictionary()) {
                xobjDict = QPDFObjectHandle::newDictionary();
            }
            const std::string resName = "/TrailerRed" + std::to_string(idx);
            xobjDict.replaceKey(resName, xobj);
            resources.replaceKey("/XObject", xobjDict);
            pageObj.replaceKey("/Resources", resources);

            // Replace the page content with a single full-page image
            // draw. Coordinates are in PDF points with bottom-left
            // origin, so translate by (mx0, my0) and scale by the
            // MediaBox dimensions.
            char buf[192];
            std::snprintf(buf, sizeof(buf), "q %.3f 0 0 %.3f %.3f %.3f cm %s Do Q\n", widthPts,
                          heightPts, mx0, my0, resName.c_str());
            QPDFObjectHandle newContent =
                QPDFObjectHandle::newStream(m_qpdf.get(), std::string(buf));
            pageObj.replaceKey("/Contents", newContent);

            // Kill any annotations on this page — they referenced
            // content we just rasterised over. Users won't expect an
            // Ink stroke or Text Box to survive a redaction pass on
            // the same page.
            pageObj.removeKey("/Annots");
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool PdfEditor::writeAnnotations(const std::vector<Annotation> &annotations) {
    if (!m_valid)
        return false;
    if (annotations.empty())
        return true;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        for (int p = 0; p < total; ++p) {
            std::vector<QPDFObjectHandle> toAdd;
            QPDFPageObjectHelper &page = pages[static_cast<size_t>(p)];
            QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
            if (!media.isArray() || media.getArrayNItems() < 4)
                continue;
            const double my0 = media.getArrayItem(1).getNumericValue();
            const double my1 = media.getArrayItem(3).getNumericValue();
            const double pageHeight = my1 - my0;
            for (const Annotation &a : annotations) {
                if (a.page != p)
                    continue;
                QPDFObjectHandle dict = buildAnnotation(*m_qpdf, a, pageHeight);
                if (dict.isDictionary()) {
                    toAdd.push_back(m_qpdf->makeIndirectObject(dict));
                }
            }
            if (toAdd.empty())
                continue;
            QPDFObjectHandle pageObj = page.getObjectHandle();
            QPDFObjectHandle annots = pageObj.getKey("/Annots");
            if (!annots.isArray()) {
                annots = QPDFObjectHandle::newArray();
            }
            for (const auto &item : toAdd) {
                annots.appendItem(item);
            }
            pageObj.replaceKey("/Annots", annots);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

namespace {

QColor colourFromArray(QPDFObjectHandle arr) {
    if (!arr.isArray() || arr.getArrayNItems() < 3)
        return QColor();
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
    if (!arr.isArray() || arr.getArrayNItems() < 4)
        return {};
    const double x1 = arr.getArrayItem(0).getNumericValue();
    const double y1 = arr.getArrayItem(1).getNumericValue();
    const double x2 = arr.getArrayItem(2).getNumericValue();
    const double y2 = arr.getArrayItem(3).getNumericValue();
    const double left = std::min(x1, x2);
    const double right = std::max(x1, x2);
    const double bottom = std::min(y1, y2);
    const double top = std::max(y1, y2);
    // Flip back to top-left origin.
    return QRectF(QPointF(left, pageHeight - top), QPointF(right, pageHeight - bottom));
}

} // namespace

std::vector<Annotation> PdfEditor::readAnnotations() const {
    std::vector<Annotation> out;
    if (!m_valid)
        return out;
    try {
        auto pages = QPDFPageDocumentHelper(*m_qpdf).getAllPages();
        const int total = static_cast<int>(pages.size());
        for (int p = 0; p < total; ++p) {
            QPDFPageObjectHelper &page = pages[static_cast<size_t>(p)];
            QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
            if (!media.isArray() || media.getArrayNItems() < 4)
                continue;
            const double my0 = media.getArrayItem(1).getNumericValue();
            const double my1 = media.getArrayItem(3).getNumericValue();
            const double pageHeight = my1 - my0;
            auto flipY = [pageHeight](double y) { return pageHeight - y; };

            QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
            if (!annots.isArray())
                continue;
            const int n = annots.getArrayNItems();
            for (int i = 0; i < n; ++i) {
                QPDFObjectHandle entry = annots.getArrayItem(i);
                if (!entry.isDictionary())
                    continue;
                QPDFObjectHandle subtype = entry.getKey("/Subtype");
                if (!subtype.isName())
                    continue;
                const std::string st = subtype.getName();

                Annotation a;
                a.page = p;
                a.bounds = rectFromArray(entry.getKey("/Rect"), pageHeight);
                QColor stroke = colourFromArray(entry.getKey("/C"));
                if (stroke.isValid())
                    a.style.stroke = stroke;
                QPDFObjectHandle bs = entry.getKey("/BS");
                if (bs.isDictionary()) {
                    QPDFObjectHandle w = bs.getKey("/W");
                    if (w.isNumber())
                        a.style.strokeWidth = w.getNumericValue();
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
                    if (!L.isArray() || L.getArrayNItems() < 4)
                        continue;
                    const double x1 = L.getArrayItem(0).getNumericValue();
                    const double y1 = L.getArrayItem(1).getNumericValue();
                    const double x2 = L.getArrayItem(2).getNumericValue();
                    const double y2 = L.getArrayItem(3).getNumericValue();
                    a.points = {QPointF(x1, flipY(y1)), QPointF(x2, flipY(y2))};
                    QPDFObjectHandle le = entry.getKey("/LE");
                    bool isArrow = false;
                    if (le.isArray() && le.getArrayNItems() >= 2) {
                        QPDFObjectHandle end = le.getArrayItem(1);
                        if (end.isName() && end.getName() != "/None")
                            isArrow = true;
                    }
                    a.type = isArrow ? AnnotationType::Arrow : AnnotationType::Line;
                } else if (st == "/Ink") {
                    QPDFObjectHandle inkList = entry.getKey("/InkList");
                    if (!inkList.isArray() || inkList.getArrayNItems() < 1)
                        continue;
                    QPDFObjectHandle inkPts = inkList.getArrayItem(0);
                    if (!inkPts.isArray())
                        continue;
                    const int sn = inkPts.getArrayNItems();
                    for (int k = 0; k + 1 < sn; k += 2) {
                        const double x = inkPts.getArrayItem(k).getNumericValue();
                        const double y = inkPts.getArrayItem(k + 1).getNumericValue();
                        a.points.emplace_back(x, flipY(y));
                    }
                    if (a.points.size() < 2)
                        continue;
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
                } else if (st == "/Highlight" || st == "/Underline" || st == "/StrikeOut") {
                    a.type = st == "/Highlight"   ? AnnotationType::Highlight
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
                            a.quads.emplace_back(QPointF(std::min(x1, x2), flipY(y1)),
                                                 QPointF(std::max(x1, x2), flipY(y3)));
                        }
                    }
                    if (a.quads.empty())
                        a.quads.push_back(a.bounds);
                } else {
                    continue;
                }
                out.push_back(std::move(a));
            }
        }
    } catch (const std::exception &) {
        return out;
    }
    return out;
}

// ---------------------------------------------------------------------------
// AcroForm field access (Phase 5)
// ---------------------------------------------------------------------------

bool PdfEditor::hasFormFields() const {
    if (!m_valid)
        return false;
    try {
        QPDFAcroFormDocumentHelper afdh(*m_qpdf);
        return !afdh.getFormFields().empty();
    } catch (const std::exception &) {
        return false;
    }
}

std::vector<FormField> PdfEditor::readFormFields() const {
    std::vector<FormField> result;
    if (!m_valid)
        return result;
    try {
        QPDFAcroFormDocumentHelper afdh(*m_qpdf);
        QPDFPageDocumentHelper pdh(*m_qpdf);
        auto pages = pdh.getAllPages(); // non-const — getAnnotations() is non-const
        const int pageCount = static_cast<int>(pages.size());

        // Build objId → page-index so we can locate each widget's page cheaply.
        std::map<int, int> annotToPage;
        for (int pi = 0; pi < pageCount; ++pi) {
            for (auto &annot : pages[static_cast<size_t>(pi)].getAnnotations()) {
                annotToPage[annot.getObjectHandle().getObjGen().getObj()] = pi;
            }
        }

        int id = 0;
        for (auto &field : afdh.getFormFields()) {
            // Skip push-buttons — they have no user-editable value.
            if (field.isPushbutton()) {
                ++id;
                continue;
            }

            FormField ff;
            ff.id = id++;
            ff.name = QString::fromStdString(field.getFullyQualifiedName());
            ff.label = QString::fromStdString(field.getAlternativeName());

            if (field.isText()) {
                ff.type = FormFieldType::Text;
                const int flags = field.getFlags();
                ff.multiline = (flags & ff_tx_multiline) != 0;
                ff.isPassword = (flags & ff_tx_password) != 0;
            } else if (field.isCheckbox()) {
                ff.type = FormFieldType::Checkbox;
            } else if (field.isRadioButton()) {
                ff.type = FormFieldType::RadioButton;
            } else if (field.isChoice()) {
                ff.type = FormFieldType::Dropdown;
                for (const auto &opt : field.getChoices())
                    ff.options << QString::fromStdString(opt);
            } else {
                ff.type = FormFieldType::Unknown;
            }

            // Value
            if (field.isCheckbox() || field.isRadioButton()) {
                // QPDFFormFieldObjectHelper::isChecked() was added in qpdf
                // 11.10, but Ubuntu noble's libqpdf-dev (the LTS line CI
                // runs on) ships 11.9. Replicate the helper inline so the
                // build links against 11.0+. Per the PDF spec a checkbox
                // is "on" when its /V entry is a Name other than /Off.
                //
                // `v` is intentionally non-const: QPDFObjectHandle::isName()
                // and getName() weren't const-qualified until qpdf 12.
                QPDFObjectHandle v = field.getValue();
                const bool checked = v.isName() && v.getName() != "/Off";
                ff.value = checked ? QStringLiteral("Yes") : QStringLiteral("Off");
            } else {
                ff.value = QString::fromStdString(field.getValueAsString());
            }

            // Common flags
            const int flags = field.getFlags();
            ff.readOnly = (flags & ff_all_read_only) != 0;
            ff.required = (flags & ff_all_required) != 0;

            // Rect and page from the first widget annotation.
            auto annots = afdh.getAnnotationsForField(field);
            if (!annots.empty()) {
                const auto r = annots.front().getRect(); // non-const method
                ff.rectPts = QRectF(r.llx, r.lly, r.urx - r.llx, r.ury - r.lly);
                const int oid = annots.front().getObjectHandle().getObjGen().getObj();
                const auto it = annotToPage.find(oid);
                if (it != annotToPage.end())
                    ff.page = it->second;
            }

            result.push_back(std::move(ff));
        }
    } catch (const std::exception &) {
    }
    return result;
}

bool PdfEditor::setFormFieldValue(int id, const QString &value) {
    if (!m_valid)
        return false;
    try {
        QPDFAcroFormDocumentHelper afdh(*m_qpdf);
        auto fields = afdh.getFormFields();
        // id maps to the same positional walk used by readFormFields(),
        // skipping push-buttons.
        int idx = 0;
        for (auto &field : fields) {
            if (field.isPushbutton()) {
                ++idx;
                continue;
            }
            if (idx == id) {
                if (field.getFlags() & ff_all_read_only)
                    return false;
                field.setV(value.toStdString(), /*need_appearances=*/true);
                afdh.setNeedAppearances(true);
                return true;
            }
            ++idx;
        }
        return false; // id out of range
    } catch (const std::exception &) {
        return false;
    }
}

// ---------------------------------------------------------------------------

bool PdfEditor::save(const QString &path) {
    return saveImpl(path, nullptr);
}

bool PdfEditor::save(const QString &path, const EncryptionOptions &enc) {
    return saveImpl(path, &enc);
}

// Write a linearized, stream-compressed copy of the document.
// qpdf's defaults already re-compress streams, but we ask explicitly so
// the behaviour is obvious from the call site. Object streams cut the
// indirect-object overhead that accumulates in docs that have been
// round-tripped through other editors; linearization lets readers
// show the first page before downloading the rest.
bool PdfEditor::saveReduced(const QString &path) {
    if (!m_valid)
        return false;
    try {
        QPDFWriter writer(*m_qpdf, path.toLocal8Bit().constData());
        writer.setStaticID(false);
        writer.setLinearization(true);
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setStreamDataMode(qpdf_s_compress);
        writer.setCompressStreams(true);
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool PdfEditor::saveImpl(const QString &path, const EncryptionOptions *enc) {
    if (!m_valid)
        return false;
    try {
        QPDFWriter writer(*m_qpdf, path.toLocal8Bit().constData());
        writer.setStaticID(false);
        if (enc) {
            // qpdf expects both passwords as C strings. An empty user
            // password is the well-defined "anyone can open but
            // permissions still apply" case.
            const QByteArray user = enc->userPassword.toUtf8();
            const QByteArray owner =
                enc->ownerPassword.isEmpty() ? user : enc->ownerPassword.toUtf8();
            const qpdf_r3_print_e printLevel =
                !enc->allowPrint ? qpdf_r3p_none
                                 : (enc->allowHighResPrint ? qpdf_r3p_full : qpdf_r3p_low);
            // Use R6 (AES-256) — the only password scheme that's still
            // considered secure and the only one the current PDF spec
            // sanctions. Older Rx methods are available on qpdf via
            // *Insecure-suffixed functions; we deliberately don't
            // expose them.
            writer.setR6EncryptionParameters(user.constData(), owner.constData(),
                                             enc->allowAccessibility, enc->allowExtract,
                                             enc->allowModify, // allow_assemble
                                             enc->allowAnnotate, enc->allowFormFilling,
                                             enc->allowModify, // allow_modify_other
                                             printLevel,
                                             /*encrypt_metadata_aes=*/true);
        }
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace trailer
