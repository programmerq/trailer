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

    // Directory holding the macOS "Quit and Keep Windows" draft store —
    // the serialized open-window set plus the bytes of any unsaved/
    // untitled documents kept across a relaunch (see SessionDraftStore and
    // docs/decision-records/2026-07-16-quit-and-keep-windows.md). Sits
    // beside settings.toml / recent.json under the app data dir.
    static QString sessionDraftsDir();

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
