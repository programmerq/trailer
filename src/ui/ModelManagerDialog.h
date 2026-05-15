#pragma once

#include "ml/ModelRegistry.h"

#include <QList>
#include <QString>
#include <functional>

class QProgressDialog;
class QWidget;

namespace trailer {

class Application;

// Per-model "never download" policy. Stored under the [first_use]
// table of settings.toml (legacy bag, see Settings.h) with keys of
// the form `ml_never_download_<modelIdKey>`.
namespace ModelPolicy {

bool isNeverDownload(Application *app, ModelId id);
void setNeverDownload(Application *app, ModelId id, bool enabled);
QString flagKey(ModelId id); // exposed for tests

} // namespace ModelPolicy

// Opens the "Manage ML Models" dialog (Tools menu). Synchronous; runs
// a modal dialog loop and returns when the user closes it.
void showModelManagerDialog(QWidget *parent, Application *app);

// Pre-flight a feature whose ONNX models live in the registry: walks
// the user through any required downloads, honouring the
// "Never download" policy and offering a direct path to the manager.
//
// `isReady`/`kickoff`/`wireSignals` plug the helper into the feature's
// own wrapper (BackgroundRemover / SamSession / OcrEngine), which
// already orchestrate hash-verify + retry + multi-model bundling.
struct ModelDownloadRequest {
    Application *app = nullptr;
    QWidget *parent = nullptr;
    QList<ModelId> required;

    QString featureName;     // "Background Removal"
    QString modelLabel;      // "U²-Net Portable"
    QString licenseLabel;    // "Apache 2.0"
    QString progressMessage; // "Downloading U²-Net Portable…"
    QString failureSubject;  // "the background-removal model"

    // Whether all required models are loaded *right now*.
    std::function<bool()> isReady;
    // Start the async download. Must arrange for the signals wired by
    // `wireSignals` to fire.
    std::function<void()> kickoff;
    // Wire feature-specific signals to the helper's progress dialog
    // and outcome flags. Helper owns disconnection on dialog close.
    std::function<void(QProgressDialog *progress, bool *ready, bool *failed,
                       QString *failureMessage)>
        wireSignals;
};

// True iff all required models are ready after the call. Returns false
// on user cancel, policy block, or download failure. Does NOT fall
// through to a follow-up prompt if the user invokes the manager — the
// caller can re-poll `isModelReady()` on the next user action.
bool requestModelDownload(const ModelDownloadRequest &req);

} // namespace trailer
