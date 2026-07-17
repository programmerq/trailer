#pragma once

#include <QString>

namespace trailer {

class AppPaths {
  public:
    static QString settingsDir();
    static QString dataDir();

    static QString settingsFile();
    static QString recentFile();
    static QString cardsFile();
    static QString signaturesDir();
    static QString autofillDir();
    static QString versionsDir();
    static QString ocrCacheDir();
    static QString iccDir();
    static QString filtersDir();
    static QString pluginsDir();
    static QString logsDir();

    // Cache directory for downloaded ONNX model weights
    // (background removal, SAM, OCR, etc.). Models are large and
    // version-pinned, so they live under the data dir alongside other
    // long-lived artefacts — not the cache dir which the OS may purge.
    static QString modelsDir();

    // Root for developer UX recording sessions (one timestamped
    // subdirectory per --ux-record run; see docs/ux-recorder.md).
    // Always resolvable so the path stays testable, but nothing is
    // ever written here unless the build was configured with
    // TRAILER_ENABLE_UX_RECORDER and the recorder was started.
    static QString uxSessionsDir();

    static void ensureDirExists(const QString &path);
};

} // namespace trailer
