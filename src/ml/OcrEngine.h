#pragma once

#include <QImage>
#include <QObject>
#include <QPolygon>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace trailer {

class ModelRegistry;
class OnnxSession;

// PP-OCR powered text recognizer for Live Text on images and
// extracting searchable text off scanned pages (DESIGN §6.11 / §10
// Phase 6). Two models are loaded:
//
//   - Detector  (en_PP-OCRv3_det, ~2.4 MB) — a DBNet that emits a
//     per-pixel text probability map; we threshold + connected-
//     component-label it into axis-aligned bounding rects.
//   - Recognizer (en_PP-OCRv3_rec, ~8.9 MB) — a CRNN whose softmax
//     output [N, T, 97] we CTC-decode against the PaddleOCR English
//     dictionary (94 chars + blank + space).
//
// The direction classifier and CJK recognizer are pinned in the
// model manifest but not loaded in this phase — documents open
// upright ≥99% of the time and the recognition API can add those
// later without breaking callers.
//
// API shape mirrors BackgroundRemover / SamSession: construct with a
// shared ModelRegistry, call ensureModelsAvailable() and wait for
// modelsReady(), then recognize() on each image. Missing-model and
// inference failures return an empty QVector rather than throwing.
class OcrEngine : public QObject {
    Q_OBJECT
public:
    struct TextBlock {
        // Quadrilateral outlining the detected text region in
        // original-image pixel coordinates. Phase 6D only emits
        // axis-aligned rects (4 corner points); a later phase can
        // extend this with rotated boxes from minAreaRect without
        // changing the struct.
        QPolygon polygon;
        QString text;
        // CTC posterior geometric mean across kept (non-blank)
        // timesteps, in [0,1]. A confidence below ~0.5 usually means
        // the line is noise; the UI can filter on it.
        float confidence = 0.0f;
    };

    explicit OcrEngine(ModelRegistry* registry, QObject* parent = nullptr);
    ~OcrEngine() override;

    // True iff both the detector and the Latin recognizer are on
    // disk and their SHA-256 matches the manifest.
    bool isModelReady() const;

    // Ask the registry to fetch any missing models. Emits
    // modelsReady() once both land or modelsUnavailable() on the
    // first failure. Safe to call when the models are already cached
    // — modelsReady() still fires.
    void ensureModelsAvailable();

    // Run the full detect → recognize pipeline against `source`.
    // Coordinates in the returned blocks are in source's pixel
    // space. Returns an empty vector if the models aren't ready or
    // inference fails.
    QVector<TextBlock> recognize(const QImage& source);

    // Last inference's binarized detection map, same size as the
    // most recent `source`. Exposed for tests and debug overlays.
    // Empty if recognize() has not yet run successfully.
    QImage lastDetectionMask() const;

signals:
    void downloadProgress(qint64 received, qint64 total);
    void modelsReady();
    void modelsUnavailable(const QString& reason);

private:
    void onModelAvailable();
    bool loadDictionary();

    // Detection: resize `source` to fit within a 960-px max side on
    // multiples of 32, run the detector, threshold + label. Returns
    // axis-aligned rects in original-image coordinates.
    QVector<QRect> detectBoxes(const QImage& source);

    // Recognition: warp the box's crop into a 48×W strip, run the
    // recognizer, CTC-decode with `m_dict`. Returns an empty string
    // on failure. Writes the posterior geometric mean into `confOut`.
    QString recognizeBox(const QImage& crop, float* confOut);

    ModelRegistry* m_registry;
    std::unique_ptr<OnnxSession> m_detector;
    std::unique_ptr<OnnxSession> m_recognizer;

    // Index 0 is CTC blank; the remaining 96 classes map to the
    // PaddleOCR English dictionary plus a trailing space. Populated
    // lazily from the :/ml/ppocr_en_dict.txt resource.
    QStringList m_dict;

    QImage m_lastMask;
};

}  // namespace trailer
