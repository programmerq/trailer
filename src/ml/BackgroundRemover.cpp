#include "BackgroundRemover.h"

#include "ModelRegistry.h"
#include "OnnxSession.h"

#include <QDebug>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace trailer {

namespace {

// u2netp expects a 320×320 input (NCHW float32).
constexpr int kModelSize = 320;

// ImageNet mean/std — the u2net reference pipeline normalises with
// these. Every u2net ONNX export I've seen was trained with them.
constexpr std::array<float, 3> kMean{0.485f, 0.456f, 0.406f};
constexpr std::array<float, 3> kStd{0.229f, 0.224f, 0.225f};

// Build an NCHW float buffer from a QImage. The image is resized to
// `kModelSize×kModelSize`, converted to RGB, and normalised.
std::vector<float> makeInputTensor(const QImage& src) {
    const QImage resized = src.scaled(
        kModelSize, kModelSize,
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB888);

    std::vector<float> tensor(3 * kModelSize * kModelSize);
    // NCHW: channel plane after channel plane. This lets us write
    // linear memory without stride fiddling in the model input.
    for (int y = 0; y < kModelSize; ++y) {
        const uchar* scan = resized.constScanLine(y);
        for (int x = 0; x < kModelSize; ++x) {
            const float r = scan[x * 3 + 0] / 255.0f;
            const float g = scan[x * 3 + 1] / 255.0f;
            const float b = scan[x * 3 + 2] / 255.0f;
            const int base = y * kModelSize + x;
            tensor[0 * kModelSize * kModelSize + base] = (r - kMean[0]) / kStd[0];
            tensor[1 * kModelSize * kModelSize + base] = (g - kMean[1]) / kStd[1];
            tensor[2 * kModelSize * kModelSize + base] = (b - kMean[2]) / kStd[2];
        }
    }
    return tensor;
}

// Convert the saliency output into a QImage alpha mask. u2net emits
// 320×320 floats that aren't bounded to [0,1], so we rescale using
// the output's own min/max — the standard post-process from the
// upstream reference implementation.
QImage maskToAlpha(const std::vector<float>& mask, int outW, int outH) {
    if (mask.size() != static_cast<size_t>(kModelSize * kModelSize)) {
        return {};
    }
    float lo = mask[0];
    float hi = mask[0];
    for (float v : mask) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    const float span = (hi - lo) > 1e-6f ? (hi - lo) : 1e-6f;

    QImage alpha(kModelSize, kModelSize, QImage::Format_Grayscale8);
    for (int y = 0; y < kModelSize; ++y) {
        uchar* scan = alpha.scanLine(y);
        for (int x = 0; x < kModelSize; ++x) {
            const float v = (mask[y * kModelSize + x] - lo) / span;
            const int b = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
            scan[x] = static_cast<uchar>(b);
        }
    }
    // Smooth resize back to the source dimensions so the mask doesn't
    // alias into 320×320 pixel boundaries on a big image.
    return alpha.scaled(outW, outH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// Multiply `alpha` (grayscale, source-sized) into the ARGB32
// representation of `source`. Pixels with low mask value are
// pushed toward fully transparent so the hole is visible.
QImage applyAlpha(const QImage& source, const QImage& alpha) {
    QImage rgb = source.convertToFormat(QImage::Format_ARGB32);
    if (alpha.size() != rgb.size()) return {};
    for (int y = 0; y < rgb.height(); ++y) {
        auto* dst = reinterpret_cast<QRgb*>(rgb.scanLine(y));
        const uchar* msk = alpha.constScanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = dst[x];
            dst[x] = qRgba(qRed(px), qGreen(px), qBlue(px), msk[x]);
        }
    }
    return rgb;
}

}  // namespace

BackgroundRemover::BackgroundRemover(ModelRegistry* registry, QObject* parent)
    : QObject(parent), m_registry(registry) {
    // Fan out registry signals so feature UIs only need to know
    // about BackgroundRemover.
    connect(m_registry, &ModelRegistry::downloadProgress, this,
            [this](ModelId id, qint64 r, qint64 t) {
                if (id == ModelId::U2NetP) emit downloadProgress(r, t);
            });
    connect(m_registry, &ModelRegistry::available, this,
            [this](ModelId id, const QString&) {
                if (id == ModelId::U2NetP) emit modelReady();
            });
    connect(m_registry, &ModelRegistry::downloadFailed, this,
            [this](ModelId id, const QString& msg) {
                if (id == ModelId::U2NetP) emit modelUnavailable(msg);
            });
}

BackgroundRemover::~BackgroundRemover() = default;

bool BackgroundRemover::isModelReady() const {
    return m_registry && m_registry->isAvailable(ModelId::U2NetP);
}

void BackgroundRemover::ensureModelAvailable() {
    if (!m_registry) {
        emit modelUnavailable(tr("Model registry is not available."));
        return;
    }
    m_registry->ensureAvailable(ModelId::U2NetP);
}

QImage BackgroundRemover::remove(const QImage& source) {
    if (source.isNull()) return {};
    if (!isModelReady()) return {};

    auto session = OnnxSession::fromFile(m_registry->localPath(ModelId::U2NetP));
    if (!session) return {};

    const auto inputs = session->inputNames();
    const auto outputs = session->outputNames();
    if (inputs.isEmpty() || outputs.isEmpty()) return {};

    // u2netp uses a single input and we only need the primary
    // saliency output (index 0, named "1959" in the rembg export).
    const std::vector<float> tensor = makeInputTensor(source);

    TensorSpec in;
    const QByteArray inName = inputs.front().toUtf8();
    in.name = inName;
    in.data = tensor.data();
    in.shape = {1, 3, kModelSize, kModelSize};
    in.elementCount = static_cast<qsizetype>(tensor.size());

    const QByteArray outName = outputs.front().toUtf8();
    auto results = session->run({in}, {outName});
    if (!results || results->empty()) {
        qWarning() << "BackgroundRemover: inference failed";
        return {};
    }

    const QImage alpha = maskToAlpha(
        results->front().data, source.width(), source.height());
    if (alpha.isNull()) return {};

    return applyAlpha(source, alpha);
}

}  // namespace trailer
