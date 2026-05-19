#include "OcrEngine.h"

#include "CancellationToken.h"
#include "ModelRegistry.h"
#include "OnnxSession.h"

#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPolygon>
#include <QRect>
#include <QString>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <vector>

// Q_INIT_RESOURCE must be called from the global namespace — it
// references a `qInitResources_<name>()` free function emitted by
// rcc. Wrapping it in a non-namespaced helper lets
// OcrEngine.loadDictionary() call it from inside `namespace trailer`.
// `inline` keeps duplicate copies from conflicting if this ever gets
// included by more than one TU.
inline void initTrailerResources() {
    Q_INIT_RESOURCE(trailer);
}

namespace trailer {

namespace {

// --- Detection constants ------------------------------------------------
//
// DBNet expects ImageNet-normalised RGB. The PaddleOCR reference
// pipeline caps the longest side at 960 and rounds dimensions to the
// nearest multiple of 32 (the stride of the backbone). A smaller cap
// trades precision for speed; 960 is the published sweet-spot.
constexpr int kDetMaxSide = 960;
constexpr int kDetStride = 32;
constexpr std::array<float, 3> kDetMean{0.485f, 0.456f, 0.406f};
constexpr std::array<float, 3> kDetStd{0.229f, 0.224f, 0.225f};

// Threshold on the sigmoid probability map. PaddleOCR defaults to
// 0.3 for training and 0.6 for inference; 0.3 keeps recall high on
// faint scans.
constexpr float kDetThreshold = 0.3f;
// Ignore blobs smaller than this (in detector-output pixel space).
constexpr int kMinBlobArea = 6;

// --- Recognition constants ----------------------------------------------
//
// The recognizer's CNN expects H=48; width is dynamic but must be a
// multiple of 8. The preprocessing pipeline in PaddleOCR keeps the
// aspect ratio and pads to the next stride.
constexpr int kRecHeight = 48;
constexpr int kRecWidthStride = 8;
constexpr int kRecMinWidth = 48;
constexpr int kRecMaxWidth = 640;
// Recognition normalises to [-1, 1] via (x/255 - 0.5) / 0.5.

// --- Helpers ------------------------------------------------------------

QSize fitDetectionSize(const QSize &src) {
    int w = src.width();
    int h = src.height();
    const int longest = std::max(w, h);
    if (longest > kDetMaxSide) {
        const float scale = static_cast<float>(kDetMaxSide) / static_cast<float>(longest);
        w = std::max(kDetStride, static_cast<int>(std::round(static_cast<float>(w) * scale)));
        h = std::max(kDetStride, static_cast<int>(std::round(static_cast<float>(h) * scale)));
    }
    // Round to the next multiple of kDetStride (up, to avoid losing
    // a row of text to floor()).
    w = ((w + kDetStride - 1) / kDetStride) * kDetStride;
    h = ((h + kDetStride - 1) / kDetStride) * kDetStride;
    return {std::max(kDetStride, w), std::max(kDetStride, h)};
}

std::vector<float> makeDetInput(const QImage &src, QSize resizedSize) {
    const QImage resized = src.convertToFormat(QImage::Format_RGB888)
                               .scaled(resizedSize.width(), resizedSize.height(),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int w = resizedSize.width();
    const int h = resizedSize.height();
    const size_t plane = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<float> tensor(3 * plane);
    for (int y = 0; y < h; ++y) {
        const uchar *scan = resized.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const float r = static_cast<float>(scan[x * 3 + 0]) / 255.0f;
            const float g = static_cast<float>(scan[x * 3 + 1]) / 255.0f;
            const float b = static_cast<float>(scan[x * 3 + 2]) / 255.0f;
            const size_t idx =
                static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            tensor[0 * plane + idx] = (r - kDetMean[0]) / kDetStd[0];
            tensor[1 * plane + idx] = (g - kDetMean[1]) / kDetStd[1];
            tensor[2 * plane + idx] = (b - kDetMean[2]) / kDetStd[2];
        }
    }
    return tensor;
}

// Binarize the DBNet probability map. Returns a row-major bool
// grid matching (w, h).
std::vector<uint8_t> binarize(const std::vector<float> &probs, int w, int h, float threshold) {
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<uint8_t> out(n, 0);
    for (size_t i = 0; i < n; ++i) {
        out[i] = probs[i] >= threshold ? 1 : 0;
    }
    return out;
}

// BFS-based 4-connected component labeller. Emits axis-aligned
// bounding rects per component, skipping tiny blobs (< kMinBlobArea).
QVector<QRect> labelComponents(std::vector<uint8_t> &bin, int w, int h) {
    QVector<QRect> rects;
    const size_t wz = static_cast<size_t>(w);
    std::vector<uint8_t> visited(wz * static_cast<size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * wz + static_cast<size_t>(x);
            if (!bin[i] || visited[i])
                continue;
            std::queue<std::pair<int, int>> q;
            q.push({x, y});
            visited[i] = 1;
            int minX = x, minY = y, maxX = x, maxY = y;
            int area = 0;
            while (!q.empty()) {
                auto [cx, cy] = q.front();
                q.pop();
                ++area;
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                constexpr std::array<std::pair<int, int>, 4> dirs{
                    {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (const auto &[dx, dy] : dirs) {
                    const int nx = cx + dx;
                    const int ny = cy + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                        continue;
                    const size_t ni = static_cast<size_t>(ny) * wz + static_cast<size_t>(nx);
                    if (!bin[ni] || visited[ni])
                        continue;
                    visited[ni] = 1;
                    q.push({nx, ny});
                }
            }
            if (area < kMinBlobArea)
                continue;
            rects.append(QRect(minX, minY, maxX - minX + 1, maxY - minY + 1));
        }
    }
    return rects;
}

// Map a detector-space rect back to the original image, expanding
// slightly (15% horizontally, 20% vertically) so the recognizer
// receives a small margin around each glyph.
QRect unscaleAndExpand(const QRect &r, const QSize &detSize, const QSize &origSize) {
    const float sx = static_cast<float>(origSize.width()) / static_cast<float>(detSize.width());
    const float sy = static_cast<float>(origSize.height()) / static_cast<float>(detSize.height());
    QRect out(
        static_cast<int>(std::floor(static_cast<double>(r.x()) * static_cast<double>(sx))),
        static_cast<int>(std::floor(static_cast<double>(r.y()) * static_cast<double>(sy))),
        static_cast<int>(std::ceil(static_cast<double>(r.width()) * static_cast<double>(sx))),
        static_cast<int>(std::ceil(static_cast<double>(r.height()) * static_cast<double>(sy))));
    const int padX = std::max(1, out.width() / 7);  // ~14 %
    const int padY = std::max(1, out.height() / 5); // ~20 %
    out.adjust(-padX, -padY, padX, padY);
    out = out.intersected(QRect(QPoint(), origSize));
    return out;
}

// Sort blocks in reading order (top-to-bottom, then left-to-right).
// Rows are considered to be on the same line if their y-centres are
// within half the median block height.
void sortReadingOrder(QVector<OcrEngine::TextBlock> &blocks) {
    if (blocks.size() < 2)
        return;
    std::vector<int> heights;
    heights.reserve(static_cast<size_t>(blocks.size()));
    for (const auto &b : blocks)
        heights.push_back(b.polygon.boundingRect().height());
    std::sort(heights.begin(), heights.end());
    const int medianH = heights[heights.size() / 2];
    const int rowTol = std::max(2, medianH / 2);
    std::sort(blocks.begin(), blocks.end(),
              [rowTol](const OcrEngine::TextBlock &a, const OcrEngine::TextBlock &b) {
                  const QRect ra = a.polygon.boundingRect();
                  const QRect rb = b.polygon.boundingRect();
                  const int ay = ra.center().y();
                  const int by = rb.center().y();
                  if (std::abs(ay - by) > rowTol)
                      return ay < by;
                  return ra.left() < rb.left();
              });
}

// --- Recognition helpers ------------------------------------------------

// Target recogniser input width given the crop's aspect ratio.
int recWidthForCrop(int cropW, int cropH) {
    if (cropH <= 0)
        return kRecMinWidth;
    const float ratio = static_cast<float>(cropW) / static_cast<float>(cropH);
    int w = static_cast<int>(std::ceil(kRecHeight * ratio));
    w = ((w + kRecWidthStride - 1) / kRecWidthStride) * kRecWidthStride;
    w = std::clamp(w, kRecMinWidth, kRecMaxWidth);
    return w;
}

std::vector<float> makeRecInput(const QImage &crop, int targetW) {
    // Resize keeping aspect, then left-align into a targetW-wide
    // strip padded with black. The reference pipeline pads with 0
    // which, after normalisation, becomes -1.
    const int srcW = std::max(1, crop.width());
    const int srcH = std::max(1, crop.height());
    const float scale = static_cast<float>(kRecHeight) / static_cast<float>(srcH);
    const int resizedW =
        std::clamp(static_cast<int>(std::round(static_cast<float>(srcW) * scale)), 1, targetW);

    const QImage resized =
        crop.convertToFormat(QImage::Format_RGB888)
            .scaled(resizedW, kRecHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const size_t plane = static_cast<size_t>(kRecHeight) * static_cast<size_t>(targetW);
    std::vector<float> tensor(3 * plane, -1.0f);
    for (int y = 0; y < kRecHeight; ++y) {
        const uchar *scan = resized.constScanLine(y);
        for (int x = 0; x < resizedW; ++x) {
            const float r = static_cast<float>(scan[x * 3 + 0]) / 255.0f;
            const float g = static_cast<float>(scan[x * 3 + 1]) / 255.0f;
            const float b = static_cast<float>(scan[x * 3 + 2]) / 255.0f;
            const size_t idx =
                static_cast<size_t>(y) * static_cast<size_t>(targetW) + static_cast<size_t>(x);
            tensor[0 * plane + idx] = (r - 0.5f) / 0.5f;
            tensor[1 * plane + idx] = (g - 0.5f) / 0.5f;
            tensor[2 * plane + idx] = (b - 0.5f) / 0.5f;
        }
    }
    return tensor;
}

// CTC greedy decode. `logits` is [T, C] in row-major. Classes follow
// PaddleOCR's convention: index 0 = blank, 1..C-1 = dictionary chars
// (last entry conventionally ' '). Collapses consecutive duplicates
// and drops blanks. Returns the decoded string + the geometric mean
// of the kept timesteps' probabilities.
QString ctcDecode(const std::vector<float> &logits, int T, int C, const QStringList &dict,
                  float *confOut) {
    QString out;
    int prev = -1;
    double logConfSum = 0.0;
    int kept = 0;
    const size_t Cz = static_cast<size_t>(C);
    for (int t = 0; t < T; ++t) {
        const size_t rowBase = static_cast<size_t>(t) * Cz;
        int best = 0;
        float bestProb = logits[rowBase];
        for (int c = 1; c < C; ++c) {
            const float p = logits[rowBase + static_cast<size_t>(c)];
            if (p > bestProb) {
                bestProb = p;
                best = c;
            }
        }
        if (best == 0 || best == prev) {
            prev = best;
            continue;
        }
        prev = best;
        const int dictIndex = best - 1;
        if (dictIndex < 0 || dictIndex >= dict.size())
            continue;
        out.append(dict.at(dictIndex));
        logConfSum += static_cast<double>(std::log(std::max(bestProb, 1e-6f)));
        ++kept;
    }
    if (confOut) {
        *confOut = kept > 0 ? static_cast<float>(std::exp(logConfSum / kept)) : 0.0f;
    }
    return out;
}

} // namespace

// --- OcrEngine ---------------------------------------------------------

OcrEngine::OcrEngine(ModelRegistry *registry, QObject *parent)
    : QObject(parent), m_registry(registry) {
    connect(m_registry, &ModelRegistry::downloadProgress, this,
            [this](ModelId id, qint64 r, qint64 t) {
                if (id == ModelId::PpOcrDetector || id == ModelId::PpOcrRecognizerLatin) {
                    emit downloadProgress(r, t);
                }
            });
    connect(m_registry, &ModelRegistry::available, this, [this](ModelId id, const QString &) {
        if (id == ModelId::PpOcrDetector || id == ModelId::PpOcrRecognizerLatin) {
            onModelAvailable();
        }
    });
    connect(m_registry, &ModelRegistry::downloadFailed, this,
            [this](ModelId id, const QString &msg) {
                if (id == ModelId::PpOcrDetector || id == ModelId::PpOcrRecognizerLatin) {
                    emit modelsUnavailable(msg);
                }
            });
}

OcrEngine::~OcrEngine() = default;

bool OcrEngine::isModelReady() const {
    if (!m_registry)
        return false;
    return m_registry->isAvailable(ModelId::PpOcrDetector) &&
           m_registry->isAvailable(ModelId::PpOcrRecognizerLatin);
}

void OcrEngine::ensureModelsAvailable() {
    if (!m_registry) {
        emit modelsUnavailable(tr("Model registry is not available."));
        return;
    }
    m_registry->ensureAvailable(ModelId::PpOcrDetector);
    m_registry->ensureAvailable(ModelId::PpOcrRecognizerLatin);
}

void OcrEngine::onModelAvailable() {
    if (isModelReady())
        emit modelsReady();
}

bool OcrEngine::loadDictionary() {
    if (!m_dict.isEmpty())
        return true;
    // Qt resources compiled into a static library need an explicit
    // init call from a translation unit that lands in the final
    // binary — otherwise the linker drops qrc_trailer.cpp. Calling
    // this helper lazily on first use keeps unit-test binaries
    // (which link only trailer_core, not trailer) working.
    initTrailerResources();
    QFile f(QStringLiteral(":/ml/ppocr_en_dict.txt"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "OcrEngine: missing :/ml/ppocr_en_dict.txt resource";
        return false;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        // The dict file lists one character per line, no blank entry.
        m_dict.append(line);
    }
    // PaddleOCR conventionally appends a trailing space as the last
    // recogniser class; the 97th softmax slot is that space.
    if (m_dict.size() == 95) {
        m_dict.append(QStringLiteral(" "));
    }
    return !m_dict.isEmpty();
}

QImage OcrEngine::lastDetectionMask() const {
    return m_lastMask;
}

QVector<QRect> OcrEngine::detectBoxes(const QImage &source) {
    const QSize detSize = fitDetectionSize(source.size());
    const std::vector<float> tensor = makeDetInput(source, detSize);

    const auto inputs = m_detector->inputNames();
    const auto outputs = m_detector->outputNames();
    if (inputs.isEmpty() || outputs.isEmpty())
        return {};

    TensorSpec in;
    const QByteArray inName = inputs.front().toUtf8();
    in.name = inName;
    in.data = tensor.data();
    in.shape = {1, 3, detSize.height(), detSize.width()};
    in.elementCount = static_cast<qsizetype>(tensor.size());

    const QByteArray outName = outputs.front().toUtf8();
    auto result = m_detector->run({in}, {outName});
    if (!result || result->empty()) {
        qWarning() << "OcrEngine: detector inference failed";
        return {};
    }

    const auto &probs = result->front().data;
    const size_t detWz = static_cast<size_t>(detSize.width());
    const size_t detHz = static_cast<size_t>(detSize.height());
    const size_t needed = detWz * detHz;
    if (probs.size() < needed)
        return {};

    auto bin = binarize(probs, detSize.width(), detSize.height(), kDetThreshold);

    // Stash the detection mask (at original-image size) for debug.
    QImage mask(detSize.width(), detSize.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < detSize.height(); ++y) {
        uchar *scan = mask.scanLine(y);
        for (int x = 0; x < detSize.width(); ++x) {
            scan[x] = bin[static_cast<size_t>(y) * detWz + static_cast<size_t>(x)] ? 255 : 0;
        }
    }
    m_lastMask = mask.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QVector<QRect> detRects = labelComponents(bin, detSize.width(), detSize.height());
    QVector<QRect> out;
    out.reserve(detRects.size());
    for (const QRect &r : detRects) {
        out.append(unscaleAndExpand(r, detSize, source.size()));
    }
    return out;
}

QString OcrEngine::recognizeBox(const QImage &crop, float *confOut) {
    if (crop.isNull())
        return {};
    if (m_dict.isEmpty() && !loadDictionary())
        return {};

    const int targetW = recWidthForCrop(crop.width(), crop.height());
    const std::vector<float> tensor = makeRecInput(crop, targetW);

    const auto inputs = m_recognizer->inputNames();
    const auto outputs = m_recognizer->outputNames();
    if (inputs.isEmpty() || outputs.isEmpty())
        return {};

    TensorSpec in;
    const QByteArray inName = inputs.front().toUtf8();
    in.name = inName;
    in.data = tensor.data();
    in.shape = {1, 3, kRecHeight, targetW};
    in.elementCount = static_cast<qsizetype>(tensor.size());

    const QByteArray outName = outputs.front().toUtf8();
    auto result = m_recognizer->run({in}, {outName});
    if (!result || result->empty()) {
        qWarning() << "OcrEngine: recognizer inference failed";
        return {};
    }

    const auto &shape = result->front().shape;
    // Expected shape [1, T, C]; C == dict.size() + 1 (blank).
    if (shape.size() != 3 || shape[0] != 1)
        return {};
    const int T = static_cast<int>(shape[1]);
    const int C = static_cast<int>(shape[2]);
    if (C != m_dict.size() + 1) {
        qWarning() << "OcrEngine: recogniser class count" << C << "does not match dictionary size"
                   << m_dict.size();
        return {};
    }
    return ctcDecode(result->front().data, T, C, m_dict, confOut);
}

QVector<OcrEngine::TextBlock> OcrEngine::recognize(const QImage &source,
                                                   const CancellationToken *cancel) {
    if (source.isNull() || !isModelReady())
        return {};
    if (CancellationToken::isCancelled(cancel))
        return {};

    if (!m_detector) {
        m_detector = OnnxSession::fromFile(m_registry->localPath(ModelId::PpOcrDetector));
    }
    if (!m_recognizer) {
        m_recognizer = OnnxSession::fromFile(m_registry->localPath(ModelId::PpOcrRecognizerLatin));
    }
    if (!m_detector || !m_recognizer)
        return {};
    if (m_dict.isEmpty() && !loadDictionary())
        return {};
    if (CancellationToken::isCancelled(cancel))
        return {};

    const QVector<QRect> boxes = detectBoxes(source);
    // Check between detector and recognizer stages — detection is
    // the long pole on bigger pages and bailing here saves the
    // dominant cost when the doc closes mid-OCR.
    if (CancellationToken::isCancelled(cancel))
        return {};

    QVector<TextBlock> blocks;
    blocks.reserve(boxes.size());

    const QImage canvas = source.convertToFormat(QImage::Format_RGB888);
    for (const QRect &box : boxes) {
        if (CancellationToken::isCancelled(cancel))
            break;
        const QRect clipped = box.intersected(QRect(QPoint(), canvas.size()));
        if (clipped.width() < 4 || clipped.height() < 4)
            continue;
        const QImage crop = canvas.copy(clipped);

        float conf = 0.0f;
        const QString text = recognizeBox(crop, &conf);
        if (text.trimmed().isEmpty())
            continue;

        TextBlock tb;
        tb.polygon = QPolygon(QVector<QPoint>{clipped.topLeft(), clipped.topRight(),
                                              clipped.bottomRight(), clipped.bottomLeft()});
        tb.text = text;
        tb.confidence = conf;
        blocks.append(tb);
    }
    sortReadingOrder(blocks);
    return blocks;
}

} // namespace trailer
