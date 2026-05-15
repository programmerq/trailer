#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <memory>

namespace trailer {

class ModelDownloader;

// Stable string ids for every model Trailer knows about. New models
// get a new id; ids never change or get re-used because they pin the
// on-disk filename.
enum class ModelId {
    // Background removal
    U2NetP,
    BiRefNetLite,
    // Interactive segmentation (SAM)
    MobileSamEncoder,
    MobileSamDecoder,
    // OCR (PaddleOCR v4 mobile via RapidOCR's ONNX exports)
    PpOcrDetector,
    PpOcrDirection,
    PpOcrRecognizerLatin,
    PpOcrRecognizerCjk,
};

struct ModelSpec {
    ModelId id;
    QString displayName;
    QString fileName;
    QString url;
    QString sha256; // lowercase hex
    qint64 size;    // bytes
    QString license;
    QString homepage;
    QString purpose;           // short description shown in the Manage ML Models dialog
    QString estimatedRamLabel; // e.g. "~200 MiB while running"
};

// Stable lowercase tag for a ModelId. Used to namespace settings keys
// (policy flags, future per-model state) so persisted state survives
// enumerator reordering.
QString modelIdKey(ModelId id);

// Registry of downloadable ONNX models. Resolves a ModelId to the
// on-disk path under AppPaths::modelsDir(), and handles the download
// + SHA256 verification when the file is missing.
//
// The registry is a QObject so features can connect to download
// progress/failure signals — async-friendly via `ensureAvailable()`.
// Tests can override the built-in spec list via setManifestForTesting
// to point at local file:// URLs and fixture hashes.
class ModelRegistry : public QObject {
    Q_OBJECT

  public:
    explicit ModelRegistry(QObject *parent = nullptr);
    ~ModelRegistry() override;

    // Absolute path where a model's file lives once downloaded. Does
    // not guarantee the file exists — use isAvailable() for that.
    QString localPath(ModelId id) const;

    // True if the cached file on disk matches the registered SHA256.
    // A present-but-corrupt file returns false (and the caller can
    // overwrite via ensureAvailable).
    bool isAvailable(ModelId id) const;

    // All registered specs, ordered as in the manifest.
    QList<ModelSpec> manifest() const;
    ModelSpec spec(ModelId id) const;

    // Start an async download for a missing/corrupt model. Returns
    // false if the id is unknown or a download is already in flight.
    // The `available` or `downloadFailed` signals fire exactly once
    // per call.
    bool ensureAvailable(ModelId id);

    // Synchronous counterpart for tests and CLI tooling. Spins an
    // event loop internally; blocks until the download finishes or
    // fails. Returns true iff the file is now available and verified.
    bool ensureAvailableSync(ModelId id, int timeoutMs = 60000);

    // Override the manifest for testing. Replaces the built-in list
    // with the given specs; also resets the models directory.
    void setManifestForTesting(const QList<ModelSpec> &specs, const QString &modelsDirOverride);

  signals:
    void downloadProgress(ModelId id, qint64 received, qint64 total);
    void available(ModelId id, const QString &localPath);
    void downloadFailed(ModelId id, const QString &message);

  private:
    // Built-in manifest populated in the constructor.
    void populateBuiltin();

    // Per-id state: one downloader at a time, tracked so ensureAvailable
    // is idempotent while a fetch is in flight.
    QHash<ModelId, ModelSpec> m_specs;
    QHash<ModelId, ModelDownloader *> m_downloads;
    QString m_modelsDir;
};

// Verifies that a file at `path` matches `expectedSha256` (lowercase
// hex). Returns false if the file is missing or hashes to anything
// else. Exposed for tests.
bool verifyModelHash(const QString &path, const QString &expectedSha256);

} // namespace trailer
