#pragma once

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QPolygon>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace trailer {

class ModelRegistry;
class OnnxSession;

// MobileSAM-backed segmenter powering Instant Alpha (DESIGN §6.3.3)
// and Smart Lasso (DESIGN §6.3.6).
//
// MobileSAM is architected as a one-shot **encoder** (ViT-Tiny,
// ~28 MB, 80-120 ms on CPU) and a per-click **decoder** (~16 MB,
// <10 ms). The encoder is run once per image; its 64x64x256 feature
// embedding is cached here. Each user click invokes the decoder
// against the cached embedding — interactive clicks are "fast" only
// because of this cache.
//
// API:
//
//   SamSession s(&registry);
//   if (!s.isModelReady()) { s.ensureModelsAvailable(); /* wait for modelsReady */ }
//   s.prepare(image);                         // runs encoder once
//   QImage alpha = s.segment(positives, negatives);  // runs decoder per call
//   QPolygon poly = s.contourFromLastMask();  // for Smart Lasso
//
// `prepare()` stores the image's pixel dimensions and caches the
// encoder output internally; `segment()` expects point coordinates
// in the original image's pixel space and returns a Grayscale8
// same-size mask (255 = foreground, 0 = background) derived by
// thresholding the decoder's logit output at 0. If inference fails,
// segment() returns a null QImage.
class SamSession : public QObject {
    Q_OBJECT
  public:
    explicit SamSession(ModelRegistry *registry, QObject *parent = nullptr);
    ~SamSession() override;

    bool isModelReady() const;
    void ensureModelsAvailable();

    // Encode the input image. Stashes the resulting embedding + image
    // geometry on the session. Call once per image; subsequent
    // segment() calls reuse the cache. Returns false on failure
    // (model missing, ORT error).
    bool prepare(const QImage &source);

    // Run the decoder against the cached embedding with the given
    // prompts. `positives` and `negatives` are in original-image pixel
    // coordinates. Returns a Grayscale8 mask same-size as the prepared
    // image, or a null QImage on failure. The result is also stashed
    // so contourFromLastMask() can extract a polygon without re-running
    // inference.
    QImage segment(const QVector<QPoint> &positives, const QVector<QPoint> &negatives);

    // Convenience: apply the last mask to `source` (which should be
    // the same image passed to prepare()) as an alpha channel. Pixels
    // outside the mask get alpha=0; inside stays opaque. Returns null
    // if no segment has yet run.
    QImage applyAsAlpha(const QImage &source) const;

    // Extract a simplified polygon contour from the last mask — used
    // by Smart Lasso to produce a user-editable selection. Returns
    // an empty polygon if no segment has run or the mask is empty.
    // Coordinates are in original-image pixel space.
    QPolygon contourFromLastMask() const;

    // Pixel size of the image that prepare() was last called with.
    // Empty if prepare() has not succeeded.
    QSize preparedSize() const;

  signals:
    void downloadProgress(qint64 received, qint64 total);
    // Both encoder and decoder are on disk and verified.
    void modelsReady();
    // Download failed or registry has no URL for one of the models.
    void modelsUnavailable(const QString &reason);

  private:
    void onModelAvailable();

    ModelRegistry *m_registry;
    std::unique_ptr<OnnxSession> m_encoder;
    std::unique_ptr<OnnxSession> m_decoder;

    // Cache of the encoder output for the currently prepared image.
    std::vector<float> m_embedding; // [1,256,64,64]
    QSize m_origSize;               // prepare()'s pixel size
    float m_scale = 0.0f;           // 1024 / max(W, H)
    QImage m_lastMask;              // Grayscale8 same-size as source
};

} // namespace trailer
