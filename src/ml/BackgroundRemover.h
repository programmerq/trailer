#pragma once

#include <QImage>
#include <QObject>
#include <QString>

namespace trailer {

class CancellationToken;
class ModelRegistry;

// Removes the background from an image by producing a per-pixel
// alpha mask and applying it to the original pixels. Uses the
// U²-Net Portable model (ModelId::U2NetP) — a 4.4 MB ONNX file the
// user downloads on first use.
//
// BackgroundRemover is stateless: construct with a registry, call
// `remove(QImage)`, get a QImage back with alpha populated. If the
// model isn't cached the call returns {} and emits
// `modelUnavailable` so the UI can kick off a download.
//
// Pre-/post-processing matches the canonical u2net pipeline:
// resize input to 320×320, ImageNet mean/std normalisation, model
// produces seven multi-scale saliency maps, we take output 0,
// resize back to the original dimensions, multiply into the alpha
// channel.
class BackgroundRemover : public QObject {
    Q_OBJECT
  public:
    explicit BackgroundRemover(ModelRegistry *registry, QObject *parent = nullptr);
    ~BackgroundRemover() override;

    // True if the U²-Net file is already on disk and hashes correctly.
    // Callers should check this first; if false, call
    // `ensureModelAvailable` and connect to the signals below.
    bool isModelReady() const;

    // Kick off an async download of the model via the registry.
    // Safe to call even if the model is already present (fires
    // `modelReady` immediately in that case).
    void ensureModelAvailable();

    // Run the model. Returns a null QImage if the model is not
    // ready or inference fails. Successful result is an ARGB32
    // image the same size as the input, with background pixels
    // driven toward alpha 0. Non-background pixels keep their RGB.
    //
    // `cancel` is a cooperative cancellation token (see
    // CancellationToken.h). Defaults to nullptr — existing call
    // sites stay unchanged. The token is checked at entry and
    // right after the ONNX forward pass; an interrupted run
    // returns a null QImage.
    QImage remove(const QImage &source, const CancellationToken *cancel = nullptr);

  signals:
    // Download progress from the underlying registry.
    void downloadProgress(qint64 received, qint64 total);
    // Model file present and verified.
    void modelReady();
    // Model file missing AND ensureModelAvailable hasn't been called
    // yet (or failed). Message is human-readable.
    void modelUnavailable(const QString &reason);

  private:
    ModelRegistry *m_registry;
};

} // namespace trailer
