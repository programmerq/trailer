#include "ModelRegistry.h"

#include "ModelDownloader.h"
#include "settings/AppPaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace trailer {

namespace {

QString joinPath(const QString &base, const QString &leaf) {
    return QDir::cleanPath(base + QLatin1Char('/') + leaf);
}

} // namespace

bool verifyModelHash(const QString &path, const QString &expectedSha256) {
    if (expectedSha256.isEmpty())
        return QFile::exists(path);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return false;
    const QString got = QString::fromLatin1(hash.result().toHex()).toLower();
    return got == expectedSha256.toLower();
}

ModelRegistry::ModelRegistry(QObject *parent)
    : QObject(parent), m_modelsDir(AppPaths::modelsDir()) {
    populateBuiltin();
}

ModelRegistry::~ModelRegistry() = default;

void ModelRegistry::populateBuiltin() {
    // URLs and SHA256 hashes are populated as each feature lands so we
    // avoid pinning anything we haven't verified. ensureAvailable()
    // treats an empty url as "no source known" and fails fast — that
    // surfaces a clear error rather than trying to fetch nothing.
    //
    // When filling a row in, double-check licensing at
    // huggingface.co/<repo>/blob/main/LICENSE (or the author's repo)
    // and add to THIRD_PARTY_LICENSES.md.
    auto add = [&](ModelSpec s) { m_specs.insert(s.id, std::move(s)); };

    add({ModelId::U2NetP, QStringLiteral("U²-Net Portable"), QStringLiteral("u2netp.onnx"),
         QStringLiteral("https://github.com/danielgatis/rembg/releases/download/"
                        "v0.0.0/u2netp.onnx"),
         QStringLiteral("309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8"),
         4574861, QStringLiteral("Apache 2.0"),
         QStringLiteral("https://github.com/xuebinqin/U-2-Net"),
         QStringLiteral("Background removal (fast, small model).")});
    add({ModelId::BiRefNetLite,
         QStringLiteral("BiRefNet Lite"),
         QStringLiteral("birefnet_lite.onnx"),
         {},
         {},
         0,
         QStringLiteral("MIT"),
         QStringLiteral("https://github.com/ZhengPeng7/BiRefNet"),
         QStringLiteral("Background removal (higher quality, slower).")});
    // MobileSAM is split across a one-shot image encoder (TinyViT,
    // ~28 MB) and a per-click prompt decoder (~16 MB). The encoder
    // runs once per image load; the decoder runs for each user click.
    // Pre-exported ONNX artefacts live in Acly/MobileSAM on Hugging
    // Face (MIT, derived from the Apache-2.0 upstream weights).
    //
    // We use the `_single` decoder variant: it bakes SAM's
    // select_masks rule into the graph so we get the most-confident
    // mask back directly — the right choice for both Instant Alpha
    // (apply as alpha) and Smart Lasso (threshold → contour).
    add({ModelId::MobileSamEncoder, QStringLiteral("MobileSAM Encoder"),
         QStringLiteral("mobile_sam_encoder.onnx"),
         QStringLiteral("https://huggingface.co/Acly/MobileSAM/resolve/main/"
                        "mobile_sam_image_encoder.onnx"),
         QStringLiteral("580f5fb648ea1062c0aabc26217aed56921985f03f0cbbd852bba81d760cc749"),
         28157093, QStringLiteral("Apache 2.0 (weights) / MIT (export)"),
         QStringLiteral("https://github.com/ChaoningZhang/MobileSAM"),
         QStringLiteral("Image encoder for Instant Alpha and Smart Lasso.")});
    add({ModelId::MobileSamDecoder, QStringLiteral("MobileSAM Decoder"),
         QStringLiteral("mobile_sam_decoder.onnx"),
         QStringLiteral("https://huggingface.co/Acly/MobileSAM/resolve/main/"
                        "sam_mask_decoder_single.onnx"),
         QStringLiteral("93915fc7c993ab9d59ab8c9ccd3bce37f7509c81ab4150a74abd4d2abbd8570d"),
         16501323, QStringLiteral("Apache 2.0 (weights) / MIT (export)"),
         QStringLiteral("https://github.com/ChaoningZhang/MobileSAM"),
         QStringLiteral("Prompt decoder for Instant Alpha and Smart Lasso.")});
    // PaddleOCR ONNX exports are hosted by the RapidOCR project on
    // Hugging Face. The LFS oid on each file matches its SHA-256 so
    // the download-path hash check is a straight string compare.
    add({ModelId::PpOcrDetector, QStringLiteral("PP-OCRv3 Detector (English)"),
         QStringLiteral("pp_ocr_det.onnx"),
         QStringLiteral("https://huggingface.co/SWHL/RapidOCR/resolve/main/"
                        "PP-OCRv4/en_PP-OCRv3_det_infer.onnx"),
         QStringLiteral("f139598bc2af4e4b6fe98dec11574e30edfdd91fc94ac1425c18ace3bd5a866b"),
         2423224, QStringLiteral("Apache 2.0"),
         QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR"),
         QStringLiteral("Text detection (DBNet) — finds line boxes on a page.")});
    add({ModelId::PpOcrDirection, QStringLiteral("PP-OCR Direction Classifier"),
         QStringLiteral("pp_ocr_cls.onnx"),
         QStringLiteral("https://huggingface.co/SWHL/RapidOCR/resolve/main/"
                        "PP-OCRv3/ch_ppocr_mobile_v2.0_cls_train.onnx"),
         QStringLiteral("70581b300b83babd9e0dd1d7d74c5b006869e8796da277a70c2e405bf9d77c82"), 581639,
         QStringLiteral("Apache 2.0"), QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR"),
         QStringLiteral("Text orientation classifier (auto-rotates 180° lines).")});
    add({ModelId::PpOcrRecognizerLatin, QStringLiteral("PP-OCRv3 Recognizer (Latin)"),
         QStringLiteral("pp_ocr_rec_en.onnx"),
         QStringLiteral("https://huggingface.co/SWHL/RapidOCR/resolve/main/"
                        "PP-OCRv3/en_PP-OCRv3_rec_infer.onnx"),
         QStringLiteral("ef7abd8bd3629ae57ea2c28b425c1bd258a871b93fd2fe7c433946ade9b5d9ea"),
         8967018, QStringLiteral("Apache 2.0"),
         QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR"),
         QStringLiteral("Text recognition for Latin scripts (English, European languages).")});
    add({ModelId::PpOcrRecognizerCjk, QStringLiteral("PP-OCRv4 Recognizer (CJK)"),
         QStringLiteral("pp_ocr_rec_cjk.onnx"),
         QStringLiteral("https://huggingface.co/SWHL/RapidOCR/resolve/main/"
                        "PP-OCRv4/ch_PP-OCRv4_rec_infer.onnx"),
         QStringLiteral("48fc40f24f6d2a207a2b1091d3437eb3cc3eb6b676dc3ef9c37384005483683b"),
         10857958, QStringLiteral("Apache 2.0"),
         QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR"),
         QStringLiteral("Text recognition for Chinese/Japanese/Korean scripts.")});
}

QString ModelRegistry::localPath(ModelId id) const {
    const auto it = m_specs.constFind(id);
    if (it == m_specs.constEnd())
        return {};
    return joinPath(m_modelsDir, it->fileName);
}

bool ModelRegistry::isAvailable(ModelId id) const {
    const auto it = m_specs.constFind(id);
    if (it == m_specs.constEnd())
        return false;
    return verifyModelHash(localPath(id), it->sha256);
}

QList<ModelSpec> ModelRegistry::manifest() const {
    return m_specs.values();
}

ModelSpec ModelRegistry::spec(ModelId id) const {
    return m_specs.value(id);
}

bool ModelRegistry::ensureAvailable(ModelId id) {
    const auto it = m_specs.constFind(id);
    if (it == m_specs.constEnd())
        return false;

    // Already present and hashes match — signal available directly so
    // feature code can treat ensureAvailable() as "guarantee callback fires".
    const QString dest = localPath(id);
    if (verifyModelHash(dest, it->sha256)) {
        // Fire on the event loop instead of synchronously so callers
        // get consistent ordering: connect, ensureAvailable, signal.
        QMetaObject::invokeMethod(
            this, [this, id, dest]() { emit available(id, dest); }, Qt::QueuedConnection);
        return true;
    }

    if (m_downloads.contains(id))
        return true; // already in flight
    if (it->url.isEmpty()) {
        QMetaObject::invokeMethod(
            this,
            [this, id]() {
                emit downloadFailed(id, tr("No download URL registered for this model. "
                                           "Check Manage ML Models, or install the file "
                                           "manually under the models directory."));
            },
            Qt::QueuedConnection);
        return false;
    }

    auto *dl = new ModelDownloader(this);
    m_downloads.insert(id, dl);

    connect(dl, &ModelDownloader::progress, this,
            [this, id](qint64 rec, qint64 total) { emit downloadProgress(id, rec, total); });
    connect(dl, &ModelDownloader::finished, this, [this, id, dl](const QString &path) {
        m_downloads.remove(id);
        dl->deleteLater();
        emit available(id, path);
    });
    connect(dl, &ModelDownloader::failed, this, [this, id, dl](const QString &message) {
        m_downloads.remove(id);
        dl->deleteLater();
        emit downloadFailed(id, message);
    });

    dl->start(it->url, dest, it->sha256);
    return true;
}

bool ModelRegistry::ensureAvailableSync(ModelId id, int timeoutMs) {
    if (isAvailable(id))
        return true;
    QEventLoop loop;
    bool ok = false;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(this, &ModelRegistry::available, &loop, [&](ModelId gotId, const QString &) {
        if (gotId == id) {
            ok = true;
            loop.quit();
        }
    });
    connect(this, &ModelRegistry::downloadFailed, &loop, [&](ModelId gotId, const QString &) {
        if (gotId == id) {
            ok = false;
            loop.quit();
        }
    });
    connect(&timeout, &QTimer::timeout, &loop, [&] {
        ok = false;
        loop.quit();
    });
    if (!ensureAvailable(id))
        return false;
    timeout.start(timeoutMs);
    loop.exec();
    return ok;
}

void ModelRegistry::setManifestForTesting(const QList<ModelSpec> &specs,
                                          const QString &modelsDirOverride) {
    m_specs.clear();
    for (const auto &s : specs) {
        m_specs.insert(s.id, s);
    }
    if (!modelsDirOverride.isEmpty()) {
        m_modelsDir = modelsDirOverride;
    }
}

} // namespace trailer
