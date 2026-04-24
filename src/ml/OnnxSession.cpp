#include "OnnxSession.h"

#include <onnxruntime_cxx_api.h>

#include <QDebug>
#include <QFile>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <numeric>

namespace trailer {

namespace {

// Shared ORT environment. One per process is the documented pattern —
// it owns the thread pool and logging sink. We build it lazily on the
// first call so unit tests that never use ONNX don't pay for it.
//
// The env is intentionally leaked (raw `new`, never deleted): ORT's
// destructor tears down its internal thread pool, which on some
// platforms interacts badly with C++ static-destructor ordering and
// has been observed to abort in `libc++`'s mutex teardown path
// during process exit. Leaking is the documented workaround and
// costs a few MB at shutdown, which the OS reclaims anyway.
Ort::Env& sharedEnv() {
    static Ort::Env* env = new Ort::Env(
        ORT_LOGGING_LEVEL_WARNING, "trailer");
    return *env;
}

// Pick a sensible default thread count. ORT defaults to the full
// hardware concurrency, which is too aggressive for a desktop app
// that may be running in the background. Cap at 4 — the models we
// ship (u2netp, MobileSAM, PP-OCRv4 mobile) don't meaningfully scale
// past that.
int defaultThreads() {
    const int hw = QThread::idealThreadCount();
    if (hw <= 0) return 1;
    return std::min(hw, 4);
}

Ort::SessionOptions makeOptions() {
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(defaultThreads());
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    return opts;
}

}  // namespace

struct OnnxSession::Impl {
    // The session holds borrowed references to the env; env lifetime
    // is process-static so that's safe. Session itself owns its
    // allocators and thread pool.
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;

    // Cached name storage (ORT::AllocatedStringPtr owns the buffer;
    // we keep the owned strings and hand out string_view-ish refs).
    std::vector<Ort::AllocatedStringPtr> inputNamesOwned;
    std::vector<Ort::AllocatedStringPtr> outputNamesOwned;
    std::vector<const char*> inputNameCStrs;
    std::vector<const char*> outputNameCStrs;

    void cacheNames() {
        const size_t nin = session->GetInputCount();
        inputNamesOwned.reserve(nin);
        inputNameCStrs.reserve(nin);
        for (size_t i = 0; i < nin; ++i) {
            auto name = session->GetInputNameAllocated(i, allocator);
            inputNameCStrs.push_back(name.get());
            inputNamesOwned.push_back(std::move(name));
        }
        const size_t nout = session->GetOutputCount();
        outputNamesOwned.reserve(nout);
        outputNameCStrs.reserve(nout);
        for (size_t i = 0; i < nout; ++i) {
            auto name = session->GetOutputNameAllocated(i, allocator);
            outputNameCStrs.push_back(name.get());
            outputNamesOwned.push_back(std::move(name));
        }
    }
};

OnnxSession::OnnxSession() : m_impl(std::make_unique<Impl>()) {}
OnnxSession::~OnnxSession() = default;

std::unique_ptr<OnnxSession> OnnxSession::fromFile(const QString& modelPath) {
    QFile f(modelPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "OnnxSession: cannot open model" << modelPath
                   << f.errorString();
        return {};
    }
    const QByteArray bytes = f.readAll();
    f.close();
    return fromBytes(bytes);
}

std::unique_ptr<OnnxSession> OnnxSession::fromBytes(const QByteArray& modelBytes) {
    if (modelBytes.isEmpty()) return {};
    std::unique_ptr<OnnxSession> wrap(new OnnxSession());
    try {
        wrap->m_impl->session = std::make_unique<Ort::Session>(
            sharedEnv(),
            modelBytes.constData(),
            static_cast<size_t>(modelBytes.size()),
            makeOptions());
        wrap->m_impl->cacheNames();
    } catch (const Ort::Exception& e) {
        qWarning() << "OnnxSession: failed to load model:" << e.what();
        return {};
    }
    return wrap;
}

QStringList OnnxSession::inputNames() const {
    QStringList out;
    out.reserve(static_cast<int>(m_impl->inputNameCStrs.size()));
    for (const char* n : m_impl->inputNameCStrs) {
        out.push_back(QString::fromUtf8(n));
    }
    return out;
}

QStringList OnnxSession::outputNames() const {
    QStringList out;
    out.reserve(static_cast<int>(m_impl->outputNameCStrs.size()));
    for (const char* n : m_impl->outputNameCStrs) {
        out.push_back(QString::fromUtf8(n));
    }
    return out;
}

std::optional<std::vector<TensorResult>> OnnxSession::run(
    const std::vector<TensorSpec>& inputs,
    const std::vector<QByteArray>& outputs) const {
    if (!m_impl->session) return std::nullopt;

    // Wrap each input tensor in an Ort::Value backed by the caller's
    // buffer. We do NOT copy; the buffer must outlive the run() call.
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<const char*> inputNameCs;
    std::vector<Ort::Value> inputValues;
    inputNameCs.reserve(inputs.size());
    inputValues.reserve(inputs.size());

    for (const auto& spec : inputs) {
        const qsizetype declared = spec.elementCount;
        const int64_t computed = std::accumulate(
            spec.shape.begin(), spec.shape.end(), int64_t{1},
            std::multiplies<int64_t>{});
        if (computed != declared) {
            qWarning() << "OnnxSession::run: element-count mismatch for input"
                       << spec.name << "declared" << declared << "shape-product" << computed;
            return std::nullopt;
        }
        inputNameCs.push_back(spec.name.constData());
        inputValues.push_back(Ort::Value::CreateTensor<float>(
            memInfo,
            const_cast<float*>(spec.data),
            static_cast<size_t>(spec.elementCount),
            spec.shape.data(),
            spec.shape.size()));
    }

    // Output names: either the caller-specified subset or all of them.
    std::vector<const char*> outputNameCs;
    std::vector<QByteArray> ownedNames;  // storage for dynamically built names
    if (outputs.empty()) {
        for (const char* n : m_impl->outputNameCStrs) {
            outputNameCs.push_back(n);
        }
    } else {
        outputNameCs.reserve(outputs.size());
        ownedNames = outputs;  // keep backing memory alive
        for (const auto& n : ownedNames) {
            outputNameCs.push_back(n.constData());
        }
    }

    std::vector<Ort::Value> results;
    try {
        results = m_impl->session->Run(
            Ort::RunOptions{nullptr},
            inputNameCs.data(), inputValues.data(), inputValues.size(),
            outputNameCs.data(), outputNameCs.size());
    } catch (const Ort::Exception& e) {
        qWarning() << "OnnxSession::run: ORT exception:" << e.what();
        return std::nullopt;
    }

    std::vector<TensorResult> out;
    out.reserve(results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        TensorResult r;
        r.name = outputNameCs[i];
        auto info = results[i].GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        r.shape.assign(shape.begin(), shape.end());
        const size_t nelem = info.GetElementCount();
        r.data.resize(nelem);
        if (nelem > 0) {
            std::memcpy(r.data.data(),
                        results[i].GetTensorData<float>(),
                        nelem * sizeof(float));
        }
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace trailer
