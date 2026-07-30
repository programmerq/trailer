#pragma once

// Local, no-network diagnostic report the user can trigger from the Help
// menu ("Feedback Report…", wired in MainWindow::buildMenus) while
// dogfooding, to hand to a coding agent or paste into a GitHub issue.
// The dialog itself is FeedbackDialog (src/ui/FeedbackDialog.h). This
// is NOT a telemetry
// pipeline: nothing here is ever sent anywhere. Gathering (reading live
// app/window/document state) and formatting (turning a snapshot into
// markdown text) are deliberately split so the formatting logic — the
// part with actual branching/edge cases — is unit-testable without a
// GUI: build an AppSnapshot by hand, call formatMarkdown(), assert on
// the string. Only gatherSnapshot() touches live Qt widgets/Application
// state.

#include "document/IDocument.h"

#include <QList>
#include <QString>

namespace trailer {

class Application;

namespace feedback {

// One open document, as seen from a single window's tab strip.
struct DocumentSnapshot {
    // Always available and always safe to show: whatever IDocument::
    // displayName() reports (already a bare filename for real files,
    // "Untitled" for scratch documents — never a full path).
    QString displayName;
    // Full path, may be empty for an untitled/clipboard-origin document.
    // Only rendered into the report when the user opts in (see
    // formatMarkdown's includeFullPaths parameter) — see the privacy
    // note on that function.
    QString filePath;
    DocumentType type = DocumentType::Unknown;
    bool dirty = false;
    bool untitled = false;
    // 0 when the format doesn't expose a page count (bare image).
    int pageCount = 0;
    int currentPage = 0;
    bool supportsZoom = false;
    ZoomMode zoomMode = ZoomMode::Custom;
    double zoomFactor = 1.0;
    bool supportsViewModes = false;
    ViewMode viewMode = ViewMode::SinglePage;
    bool hasTextLayer = false;
};

// One open MainWindow.
struct WindowSnapshot {
    int currentDocumentIndex = -1;
    bool sidebarVisible = false;
    bool markupToolbarVisible = false;
    bool formToolbarVisible = false;
    QList<DocumentSnapshot> documents;
};

// One ONNX model the ML feature set depends on.
struct ModelSnapshot {
    QString displayName;
    bool available = false;
    // The user's per-model "never download" policy (ModelManagerDialog).
    bool neverDownload = false;
};

// Everything the report renders. Plain data — no QWidget, no
// Application pointer — so it can be built by hand in a unit test.
struct AppSnapshot {
    // --- stable, greppable header -----------------------------------
    QString appName = QStringLiteral("Trailer");
    // TRAILER_VERSION_STRING: for a "-dev" build in a git checkout this
    // already carries "+<commit-count>.g<sha>[.dirty]" (see
    // CMakeLists.txt's version-derivation block), so it doubles as the
    // commit identifier for dev builds. Release builds show the bare
    // X.Y.Z tag instead; commitSha is then left empty and the report
    // says so explicitly rather than guessing.
    QString versionString;
    QString commitSha;    // parsed out of versionString; empty if not present
    bool dirtyWorkingTree = false;
    QString generatedAtUtc; // ISO 8601, e.g. 2026-07-30T18:04:11Z
    QString buildType;      // "Release" / "Debug" / "RelWithDebInfo" / etc.

    // --- platform ------------------------------------------------------
    QString qtVersion;
    QString osPrettyName;
    QString cpuArchitecture;

    // --- app-wide settings/state ---------------------------------------
    QString themeSetting;          // "System" / "Light" / "Dark"
    QString effectiveColorScheme;  // "Light" / "Dark" / "Unknown"
    bool autoSaveEnabled = true;
    QString openFilesIn; // "New Tab" / "New Window" / "Same Window"
    int recentFilesCount = 0; // count only, never the entries themselves

    bool mlRecognizeTextInBackground = false;
    bool mlPreloadSegmentationOnToolActivation = false;
    bool mlRunOnBattery = false;
    QList<ModelSnapshot> models;

    QList<WindowSnapshot> windows;
};

// Build a snapshot from the live application. Reads Application::
// settings()/recentFiles()/modelRegistry()/windows() and each
// MainWindow's document list; degrades field-by-field rather than
// failing outright — an app with zero windows or a document type that
// doesn't support zoom still yields a usable (if sparser) report. Never
// touches the network.
AppSnapshot gatherSnapshot(Application *app);

// Render `snapshot` as GitHub-flavoured markdown.
//
// Privacy: `includeFullPaths` controls whether DocumentSnapshot::filePath
// (a full on-disk path) is rendered, or just the bare displayName. It
// defaults to false — full paths often encode information the user
// wouldn't think to redact before pasting into a public issue (a home
// directory username, a client/company name in a folder path, a OneDrive/
// Dropbox sync path). The report is still useful for debugging with just
// file types, page counts, and basenames; the user opts into full paths
// deliberately (a checkbox in FeedbackDialog) rather than the default
// surprising them with more than they meant to share.
QString formatMarkdown(const AppSnapshot &snapshot, bool includeFullPaths = false);

} // namespace feedback
} // namespace trailer
