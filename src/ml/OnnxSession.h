#pragma once

#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace Ort {
class Env;
class Session;
class MemoryInfo;
class Value;
}  // namespace Ort

namespace trailer {

// Thin RAII wrapper around Ort::Session that hides the ORT C++ API
// from the rest of the codebase. The ML feature modules
// (BackgroundRemover, SamSegmenter, OcrEngine, …) consume only this
// header + QImage/QByteArray and never reach for onnxruntime_cxx_api.h
// directly — which keeps the ORT include blast-radius small and lets
// us swap implementations (GPU, quantised, remote) later without
// touching feature code.
//
// The env and allocator are shared-singleton-ish: you can construct as
// many sessions as you like and they'll all share the same `Ort::Env`
// via the global accessor below. That matches ORT's recommendation and
// keeps per-session memory overhead to a few MB.

// Input/output tensor view. Data is non-owning: the caller keeps the
// float buffer alive for the duration of the run() call. Shape is in
// NCHW-or-whatever the model expects; OnnxSession does not interpret it.
struct TensorSpec {
    QByteArray name;
    const float* data;
    std::vector<int64_t> shape;
    qsizetype elementCount;  // product of shape; sanity checked against buffer size
};

// Single output of a run() call. Data is copied out of the ORT-owned
// buffer because ORT frees the Value when the session is next used; a
// managed copy keeps downstream code simple.
struct TensorResult {
    QByteArray name;
    std::vector<int64_t> shape;
    std::vector<float> data;
};

class OnnxSession {
public:
    // Load a model from a filesystem path. Returns nullptr if the file
    // cannot be read, is malformed, or does not load in ORT.
    static std::unique_ptr<OnnxSession> fromFile(const QString& modelPath);

    // Load a model from an in-memory buffer. Useful for models embedded
    // in resources/ or generated at test time.
    static std::unique_ptr<OnnxSession> fromBytes(const QByteArray& modelBytes);

    ~OnnxSession();

    // Execute the model. `inputs` must list every model input in any
    // order — OnnxSession matches by name. `outputs` is the list of
    // output names to read; if empty, every declared output is read.
    // Returns std::nullopt if ORT throws during the run (out-of-memory,
    // shape mismatch, etc.) and logs to qWarning.
    std::optional<std::vector<TensorResult>> run(
        const std::vector<TensorSpec>& inputs,
        const std::vector<QByteArray>& outputs = {}) const;

    // Introspection helpers used by the feature modules to validate at
    // load time that a downloaded model matches the shape they expect.
    QStringList inputNames() const;
    QStringList outputNames() const;

private:
    OnnxSession();
    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace trailer
