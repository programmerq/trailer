#include "SamSession.h"

#include "ModelRegistry.h"
#include "OnnxSession.h"

#include <QDebug>
#include <QImage>
#include <QPolygon>
#include <QStack>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace trailer {

namespace {

// MobileSAM's encoder eats a 1024x1024 normalised RGB tensor. The
// reference pipeline resizes the longest side to 1024, normalises
// in 0-255 space (NOT /255 like u2net), then pads bottom/right with
// zeros to square. The decoder always maps coordinates back via
// `orig_im_size`, so we carry the scale and original size forward.
constexpr int kEncoderSize = 1024;
constexpr std::array<float, 3> kSamMean{123.675f, 116.28f, 103.53f};
constexpr std::array<float, 3> kSamStd{58.395f, 57.12f, 57.375f};

// Mask logits come back the same size as the source image — the
// Acly export bakes unpad + resize into the ONNX graph. Threshold at
// 0 (i.e. sigmoid > 0.5) gives us the final binary mask.
constexpr float kMaskThreshold = 0.0f;

std::vector<float> makeEncoderInput(const QImage& src, float& scaleOut) {
    const float scale =
        static_cast<float>(kEncoderSize) /
        static_cast<float>(std::max(src.width(), src.height()));
    scaleOut = scale;

    const int resizedW =
        std::max(1, static_cast<int>(std::round(src.width() * scale)));
    const int resizedH =
        std::max(1, static_cast<int>(std::round(src.height() * scale)));

    const QImage resized = src
        .convertToFormat(QImage::Format_RGB888)
        .scaled(resizedW, resizedH,
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    std::vector<float> tensor(
        static_cast<size_t>(3 * kEncoderSize * kEncoderSize), 0.0f);
    const size_t plane =
        static_cast<size_t>(kEncoderSize) * kEncoderSize;
    for (int y = 0; y < resizedH; ++y) {
        const uchar* scan = resized.constScanLine(y);
        for (int x = 0; x < resizedW; ++x) {
            const float r = scan[x * 3 + 0];
            const float g = scan[x * 3 + 1];
            const float b = scan[x * 3 + 2];
            const size_t idx =
                static_cast<size_t>(y) * kEncoderSize +
                static_cast<size_t>(x);
            tensor[0 * plane + idx] = (r - kSamMean[0]) / kSamStd[0];
            tensor[1 * plane + idx] = (g - kSamMean[1]) / kSamStd[1];
            tensor[2 * plane + idx] = (b - kSamMean[2]) / kSamStd[2];
        }
    }
    return tensor;
}

// Binary threshold the decoder logits (same size as orig image) into
// a Grayscale8 QImage — 255 where foreground, 0 otherwise.
QImage maskFromLogits(const std::vector<float>& logits,
                      int origW, int origH) {
    const size_t needed =
        static_cast<size_t>(origW) * static_cast<size_t>(origH);
    if (logits.size() < needed) return {};
    QImage out(origW, origH, QImage::Format_Grayscale8);
    for (int y = 0; y < origH; ++y) {
        uchar* scan = out.scanLine(y);
        for (int x = 0; x < origW; ++x) {
            const size_t idx =
                static_cast<size_t>(y) * static_cast<size_t>(origW) +
                static_cast<size_t>(x);
            scan[x] = logits[idx] > kMaskThreshold ? 255 : 0;
        }
    }
    return out;
}

// Moore-Neighbor boundary trace. Walks the outside of the first
// foreground blob encountered. Returns pixel coordinates in CCW order
// (Qt-y-down). Not fancy — we don't handle holes, but the decoder
// tends to produce a single dominant blob for a single-point prompt.
QVector<QPoint> traceBoundary(const QImage& mask) {
    if (mask.isNull() || mask.format() != QImage::Format_Grayscale8) {
        return {};
    }
    const int w = mask.width();
    const int h = mask.height();

    auto at = [&](int x, int y) -> uchar {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return mask.constScanLine(y)[x];
    };

    // Find the top-left-most foreground pixel as the start point.
    int sx = -1, sy = -1;
    for (int y = 0; y < h && sy < 0; ++y) {
        for (int x = 0; x < w; ++x) {
            if (at(x, y)) { sx = x; sy = y; break; }
        }
    }
    if (sx < 0) return {};

    // 8-connected neighbour offsets, clockwise from east.
    static constexpr int dx[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
    static constexpr int dy[8] = { 0, 1, 1, 1, 0,-1,-1,-1};

    QVector<QPoint> pts;
    pts.reserve(static_cast<int>(2 * (w + h)));

    int cx = sx;
    int cy = sy;
    // Start direction: we entered from outside (from the left), so
    // the "back" neighbour is west; the check begins clockwise from
    // back+1 which is northwest (7). Moore's algorithm classic.
    int back = 4;  // west
    pts.append(QPoint(cx, cy));

    const int maxSteps = 8 * w * h;
    for (int step = 0; step < maxSteps; ++step) {
        int found = -1;
        for (int k = 1; k <= 8; ++k) {
            const int d = (back + k) & 7;
            const int nx = cx + dx[d];
            const int ny = cy + dy[d];
            if (at(nx, ny)) { found = d; cx = nx; cy = ny; break; }
        }
        if (found < 0) break;  // isolated pixel
        pts.append(QPoint(cx, cy));
        back = (found + 4) & 7;  // reversed direction
        if (cx == sx && cy == sy && pts.size() > 2) {
            pts.removeLast();  // don't duplicate start
            break;
        }
    }
    return pts;
}

// Perpendicular distance from point p to the line through a -> b.
float perpDist(QPoint p, QPoint a, QPoint b) {
    const float dx = static_cast<float>(b.x() - a.x());
    const float dy = static_cast<float>(b.y() - a.y());
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) {
        const float px = static_cast<float>(p.x() - a.x());
        const float py = static_cast<float>(p.y() - a.y());
        return std::sqrt(px * px + py * py);
    }
    return std::abs(dy * (p.x() - a.x()) - dx * (p.y() - a.y())) / len;
}

// Iterative Douglas-Peucker — simplifies a polyline so the maximum
// deviation from the simplified path is at most `epsilon` pixels.
QVector<QPoint> douglasPeucker(const QVector<QPoint>& pts, float epsilon) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    QVector<bool> keep(n, false);
    keep[0] = true;
    keep[n - 1] = true;
    QStack<QPair<int, int>> work;
    work.push({0, n - 1});
    while (!work.isEmpty()) {
        const auto [lo, hi] = work.pop();
        float maxDist = 0.0f;
        int maxIdx = -1;
        for (int i = lo + 1; i < hi; ++i) {
            const float d = perpDist(pts[i], pts[lo], pts[hi]);
            if (d > maxDist) { maxDist = d; maxIdx = i; }
        }
        if (maxIdx >= 0 && maxDist > epsilon) {
            keep[maxIdx] = true;
            work.push({lo, maxIdx});
            work.push({maxIdx, hi});
        }
    }
    QVector<QPoint> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (keep[i]) out.append(pts[i]);
    }
    return out;
}

}  // namespace

SamSession::SamSession(ModelRegistry* registry, QObject* parent)
    : QObject(parent), m_registry(registry) {
    // Fan out registry signals, filtered to the two MobileSAM models.
    // modelsReady only fires once both files are verified on disk;
    // modelsUnavailable fires on the first failure we see.
    connect(m_registry, &ModelRegistry::downloadProgress, this,
            [this](ModelId id, qint64 r, qint64 t) {
                if (id == ModelId::MobileSamEncoder ||
                    id == ModelId::MobileSamDecoder) {
                    emit downloadProgress(r, t);
                }
            });
    connect(m_registry, &ModelRegistry::available, this,
            [this](ModelId id, const QString&) {
                if (id == ModelId::MobileSamEncoder ||
                    id == ModelId::MobileSamDecoder) {
                    onModelAvailable();
                }
            });
    connect(m_registry, &ModelRegistry::downloadFailed, this,
            [this](ModelId id, const QString& msg) {
                if (id == ModelId::MobileSamEncoder ||
                    id == ModelId::MobileSamDecoder) {
                    emit modelsUnavailable(msg);
                }
            });
}

SamSession::~SamSession() = default;

bool SamSession::isModelReady() const {
    return m_registry &&
           m_registry->isAvailable(ModelId::MobileSamEncoder) &&
           m_registry->isAvailable(ModelId::MobileSamDecoder);
}

void SamSession::ensureModelsAvailable() {
    if (!m_registry) {
        emit modelsUnavailable(tr("Model registry is not available."));
        return;
    }
    m_registry->ensureAvailable(ModelId::MobileSamEncoder);
    m_registry->ensureAvailable(ModelId::MobileSamDecoder);
}

void SamSession::onModelAvailable() {
    // Emit modelsReady only once both files are on disk.
    if (isModelReady()) emit modelsReady();
}

QSize SamSession::preparedSize() const {
    return m_origSize;
}

bool SamSession::prepare(const QImage& source) {
    if (source.isNull() || !isModelReady()) return false;

    if (!m_encoder) {
        m_encoder = OnnxSession::fromFile(
            m_registry->localPath(ModelId::MobileSamEncoder));
    }
    if (!m_decoder) {
        m_decoder = OnnxSession::fromFile(
            m_registry->localPath(ModelId::MobileSamDecoder));
    }
    if (!m_encoder || !m_decoder) return false;

    float scale = 0.0f;
    const std::vector<float> input = makeEncoderInput(source, scale);

    const auto inputs = m_encoder->inputNames();
    const auto outputs = m_encoder->outputNames();
    if (inputs.isEmpty() || outputs.isEmpty()) return false;

    TensorSpec in;
    const QByteArray inName = inputs.front().toUtf8();
    in.name = inName;
    in.data = input.data();
    in.shape = {1, 3, kEncoderSize, kEncoderSize};
    in.elementCount = static_cast<qsizetype>(input.size());

    const QByteArray outName = outputs.front().toUtf8();
    auto results = m_encoder->run({in}, {outName});
    if (!results || results->empty()) {
        qWarning() << "SamSession: encoder inference failed";
        return false;
    }

    m_embedding = std::move(results->front().data);
    m_origSize = source.size();
    m_scale = scale;
    m_lastMask = QImage();
    return true;
}

QImage SamSession::segment(const QVector<QPoint>& positives,
                           const QVector<QPoint>& negatives) {
    if (m_embedding.empty() || !m_decoder || m_origSize.isEmpty()) return {};
    if (positives.isEmpty() && negatives.isEmpty()) return {};

    // Pack coords and labels. MobileSAM wants a padding point with
    // label -1 whenever we're not passing a box prompt.
    const int nPrompts =
        static_cast<int>(positives.size() + negatives.size());
    const int nTotal = nPrompts + 1;  // +1 for padding
    std::vector<float> coords;
    std::vector<float> labels;
    coords.reserve(static_cast<size_t>(nTotal) * 2);
    labels.reserve(static_cast<size_t>(nTotal));
    auto pushPoint = [&](QPoint p, float lbl) {
        coords.push_back(static_cast<float>(p.x()) * m_scale);
        coords.push_back(static_cast<float>(p.y()) * m_scale);
        labels.push_back(lbl);
    };
    for (const QPoint& p : positives) pushPoint(p, 1.0f);
    for (const QPoint& p : negatives) pushPoint(p, 0.0f);
    // Padding point (must be present when not using boxes).
    coords.push_back(0.0f);
    coords.push_back(0.0f);
    labels.push_back(-1.0f);

    const std::vector<float> maskInput(1 * 1 * 256 * 256, 0.0f);
    const std::vector<float> hasMaskInput{0.0f};
    const std::vector<float> origImSize{
        static_cast<float>(m_origSize.height()),
        static_cast<float>(m_origSize.width())};

    const auto inputs = m_decoder->inputNames();
    // Match by name rather than position — the decoder input order is
    // implementation-defined and future exports might re-order.
    auto findName = [&](const char* needle) -> QByteArray {
        for (const QString& n : inputs) {
            if (n.compare(QLatin1String(needle), Qt::CaseInsensitive) == 0) {
                return n.toUtf8();
            }
        }
        return {};
    };
    const QByteArray nImgEmbed = findName("image_embeddings");
    const QByteArray nCoords = findName("point_coords");
    const QByteArray nLabels = findName("point_labels");
    const QByteArray nMaskIn = findName("mask_input");
    const QByteArray nHasMask = findName("has_mask_input");
    const QByteArray nOrigSize = findName("orig_im_size");

    if (nImgEmbed.isEmpty() || nCoords.isEmpty() || nLabels.isEmpty() ||
        nMaskIn.isEmpty() || nHasMask.isEmpty() || nOrigSize.isEmpty()) {
        qWarning() << "SamSession: decoder missing expected inputs:"
                   << inputs;
        return {};
    }

    std::vector<TensorSpec> inputsVec;
    inputsVec.reserve(6);
    auto add = [&](QByteArray name, const float* data,
                   std::vector<int64_t> shape, qsizetype count) {
        TensorSpec s;
        s.name = std::move(name);
        s.data = data;
        s.shape = std::move(shape);
        s.elementCount = count;
        inputsVec.push_back(std::move(s));
    };
    add(nImgEmbed, m_embedding.data(), {1, 256, 64, 64},
        static_cast<qsizetype>(m_embedding.size()));
    add(nCoords, coords.data(), {1, nTotal, 2},
        static_cast<qsizetype>(coords.size()));
    add(nLabels, labels.data(), {1, nTotal},
        static_cast<qsizetype>(labels.size()));
    add(nMaskIn, maskInput.data(), {1, 1, 256, 256},
        static_cast<qsizetype>(maskInput.size()));
    add(nHasMask, hasMaskInput.data(), {1},
        static_cast<qsizetype>(hasMaskInput.size()));
    add(nOrigSize, origImSize.data(), {2},
        static_cast<qsizetype>(origImSize.size()));

    // Ask specifically for the `masks` output — the decoder also
    // yields `iou_predictions` and `low_res_masks` we don't need here.
    const auto outputs = m_decoder->outputNames();
    QByteArray maskOut;
    for (const QString& n : outputs) {
        if (n.compare(QLatin1String("masks"), Qt::CaseInsensitive) == 0) {
            maskOut = n.toUtf8();
            break;
        }
    }
    if (maskOut.isEmpty() && !outputs.isEmpty()) {
        maskOut = outputs.front().toUtf8();  // fall back to first output
    }
    auto results = m_decoder->run(inputsVec, {maskOut});
    if (!results || results->empty()) {
        qWarning() << "SamSession: decoder inference failed";
        return {};
    }

    // masks is [1, K, H, W]; for the _single export K=1 and the
    // tensor is already original-resolution logits.
    m_lastMask = maskFromLogits(
        results->front().data, m_origSize.width(), m_origSize.height());
    return m_lastMask;
}

QImage SamSession::applyAsAlpha(const QImage& source) const {
    if (m_lastMask.isNull() || source.isNull()) return {};
    if (source.size() != m_lastMask.size()) return {};
    QImage rgb = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < rgb.height(); ++y) {
        auto* dst = reinterpret_cast<QRgb*>(rgb.scanLine(y));
        const uchar* msk = m_lastMask.constScanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = dst[x];
            dst[x] = qRgba(qRed(px), qGreen(px), qBlue(px), msk[x]);
        }
    }
    return rgb;
}

QPolygon SamSession::contourFromLastMask() const {
    if (m_lastMask.isNull()) return {};
    const QVector<QPoint> trace = traceBoundary(m_lastMask);
    if (trace.size() < 3) return {};
    // Simplify with a 2-px epsilon — fine for Smart Lasso preview,
    // still captures the silhouette faithfully.
    const QVector<QPoint> simplified = douglasPeucker(trace, 2.0f);
    return QPolygon(simplified);
}

}  // namespace trailer
