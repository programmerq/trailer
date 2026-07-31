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
#include <QRect>
#include <QSize>
#include <QSizeF>
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

    // --- geometry (owner request: "the report should include image
    // dimensions" after a 504x375 JPEG-at-80%-zoom bug needed a separate
    // `mediainfo` run to diagnose) -----------------------------------
    // Natural pixel size for an Image document. Empty (QSize::isEmpty())
    // when genuinely unknown — either the async decode (ADR 0008) hasn't
    // produced even a header-size estimate yet, or the file failed to
    // decode. Always empty for non-Image documents; PDFs report their page
    // size in POINTS below instead, deliberately kept in a separate field
    // so formatMarkdown can never print an ambiguous bare "612 x 792" that
    // a reader can't tell is px or pt.
    QSize imagePixelSize;
    // True while imagePixelSize is a provisional estimate read from the
    // file header — IDocument::contentSizePending() — because the async
    // full-resolution decode hasn't landed yet. Only ever true for Image
    // documents; always false once the decode has settled (success or
    // failure) or for a document type that never stages its open. NOTE:
    // when this is false but pageCount is still 0 for an Image document
    // with a non-empty imagePixelSize, the header read succeeded but the
    // full pixel decode subsequently FAILED (a valid-header, corrupt-body
    // file) — formatMarkdown flags that case explicitly rather than
    // presenting the header size as a confirmed, decoded result.
    bool imageSizePending = false;
    // IDocument::imageDevicePixelRatio(): 0.0 means not applicable (non-
    // Image document); otherwise the image's own devicePixelRatio — 1.0
    // for an ordinary file open, >1 for a screenshot/clipboard capture
    // (ImageDocument::markCaptureOrigin). This is the field the owner's
    // real bug needed: a PDF at 80% zoom in an oversized window reads very
    // differently once you know whether the image itself carries a DPR
    // stamp.
    double imageDpr = 0.0;
    // Current page's size in PDF points (1/72"), for a Pdf document.
    // Empty when unavailable (locked/password-protected doc, no pages, or
    // a non-PDF document) — pageCount above already governs whether the
    // page-count text renders, and this field follows the same gate in
    // formatMarkdown.
    QSizeF pdfPageSizePts;
    // True when page 0's point size differs from the CURRENT page's —
    // flags a PDF with mixed page geometry (a scanned exhibit stapled
    // into a born-digital brief, a mix of portrait and landscape pages)
    // so a reader doesn't assume pdfPageSizePts describes every page.
    // Always false for a single-page document or when the sizes agree.
    bool pdfPageSizeVariesByPage = false;
};

// One open MainWindow.
struct WindowSnapshot {
    int currentDocumentIndex = -1;
    bool sidebarVisible = false;
    bool markupToolbarVisible = false;
    bool formToolbarVisible = false;
    QList<DocumentSnapshot> documents;

    // --- window & screen geometry (same "image dimensions" ask — the
    // owner's other complaint was "it chose a HUGE window size", which a
    // report with no window/screen numbers can't settle either) --------
    // Outer frame geometry (position + size) in logical pixels, exactly
    // QWidget::geometry(). Left at the default-constructed (invalid,
    // zero-area) QRect when unknown; formatMarkdown omits the line rather
    // than print a misleading "0x0 at (0, 0)".
    QRect windowGeometry;
    bool isMaximized = false;
    bool isFullScreen = false;
    // Full and available (excludes taskbar/menu bar/dock) geometry of the
    // screen this window currently sits on, in logical pixels. Same
    // "omit rather than fake" rule as windowGeometry.
    QRect screenGeometry;
    QRect screenAvailableGeometry;
    // devicePixelRatio of that screen. 1.0 on an ordinary display; 2.0 on
    // the owner's Retina Mac, while CI/offscreen runs typically report
    // 1.0 — exactly the kind of gap that hides DPR bugs (see
    // tests/test_image_scale.cpp and its dpr1/dpr1_5/dpr2 UAT variants).
    double screenDevicePixelRatio = 1.0;
    // Current tab's document-view VIEWPORT size in logical pixels — the
    // area fit-to-window math (FitInView/FitToWidth) actually sizes
    // against, distinct from windowGeometry (which also includes
    // toolbars/menus/chrome). Empty when there is no current document.
    QSize documentViewportSize;
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
