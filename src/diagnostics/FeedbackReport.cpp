#include "diagnostics/FeedbackReport.h"

#include "app/Application.h"
#include "document/SelectableTextStore.h"
#include "ml/ModelRegistry.h"
#include "recent/RecentFiles.h"
#include "settings/Settings.h"
#include "ui/MainWindow.h"
#include "ui/ModelManagerDialog.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QStyleHints>
#include <QSysInfo>
#include <QtGlobal>

#include "TrailerVersion.h"

namespace trailer::feedback {

namespace {

QString documentTypeToString(DocumentType type) {
    switch (type) {
    case DocumentType::Pdf:
        return QStringLiteral("PDF");
    case DocumentType::Image:
        return QStringLiteral("Image");
    case DocumentType::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString zoomModeToString(ZoomMode mode) {
    switch (mode) {
    case ZoomMode::Custom:
        return QStringLiteral("Custom");
    case ZoomMode::FitInView:
        return QStringLiteral("Fit Page");
    case ZoomMode::FitToWidth:
        return QStringLiteral("Fit Width");
    case ZoomMode::Actual:
        return QStringLiteral("Actual Size");
    }
    return QStringLiteral("Custom");
}

QString viewModeToString(ViewMode mode) {
    switch (mode) {
    case ViewMode::SinglePage:
        return QStringLiteral("Single Page");
    case ViewMode::TwoPages:
        return QStringLiteral("Two Pages");
    case ViewMode::Continuous:
        return QStringLiteral("Continuous");
    }
    return QStringLiteral("Single Page");
}

QString colorSchemeToString(Qt::ColorScheme scheme) {
    switch (scheme) {
    case Qt::ColorScheme::Light:
        return QStringLiteral("Light");
    case Qt::ColorScheme::Dark:
        return QStringLiteral("Dark");
    case Qt::ColorScheme::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString buildTypeString() {
#if defined(QT_DEBUG)
    return QStringLiteral("Debug");
#else
    return QStringLiteral("Release");
#endif
}

// Extract "<sha>[.dirty]" from a TRAILER_VERSION_STRING of the shape
// "X.Y.Z-dev+<count>.g<sha>[.dirty]" (see CMakeLists.txt's version-
// derivation block). Returns an empty sha for a clean release/RC string
// that carries no "+build.metadata" suffix.
void parseCommitInfo(const QString &versionString, QString *shaOut, bool *dirtyOut) {
    static const QRegularExpression kPattern(
        QStringLiteral("\\+\\d+\\.g([0-9a-f]+)(\\.dirty)?$"));
    const QRegularExpressionMatch m = kPattern.match(versionString);
    if (m.hasMatch()) {
        *shaOut = m.captured(1);
        *dirtyOut = m.capturedLength(2) > 0;
    } else {
        *shaOut = QString();
        *dirtyOut = false;
    }
}

DocumentSnapshot snapshotDocument(IDocument *doc) {
    DocumentSnapshot snap;
    if (!doc)
        return snap;
    snap.displayName = doc->displayName();
    snap.filePath = doc->filePath();
    snap.type = doc->documentType();
    snap.dirty = doc->isDirty();
    snap.untitled = doc->isUntitled();
    snap.pageCount = doc->pageCount();
    snap.currentPage = doc->currentPage();
    snap.supportsZoom = doc->supportsZoom();
    snap.zoomMode = doc->zoomMode();
    snap.zoomFactor = doc->zoomFactor();
    snap.supportsViewModes = doc->supportsViewModes();
    snap.viewMode = doc->viewMode();
    snap.hasTextLayer = doc->hasTextLayer();

    if (snap.type == DocumentType::Image) {
        snap.imagePixelSize = doc->imagePixelSize();
        snap.imageSizePending = doc->contentSizePending();
        snap.imageDpr = doc->imageDevicePixelRatio();
    } else if (snap.type == DocumentType::Pdf) {
        // pageCount()/currentPage() above already forced PdfDocument's
        // deferred open to settle (ensureDocLoaded), so this reads the
        // real page geometry rather than blocking again.
        snap.pdfPageSizePts = doc->pageSizeHint(snap.currentPage);
        if (snap.pageCount > 1 && !snap.pdfPageSizePts.isEmpty()) {
            const QSizeF firstPage = doc->pageSizeHint(0);
            if (!firstPage.isEmpty())
                snap.pdfPageSizeVariesByPage = (firstPage != snap.pdfPageSizePts);
        }
    }

    // Current-page text kind (see the field comments in FeedbackReport.h
    // for the full caveat) — meaningful only for the two document types
    // that can carry page content at all.
    if (snap.type == DocumentType::Pdf || snap.type == DocumentType::Image) {
        snap.currentPageHasExtractableText = doc->pageHasText(snap.currentPage);
        if (SelectableTextStore *store = doc->selectableText())
            snap.currentPageHasSelectableTextBlocks = store->hasResults(snap.currentPage);
    }

    return snap;
}

WindowSnapshot snapshotWindow(MainWindow *win) {
    WindowSnapshot snap;
    if (!win)
        return snap;
    const int count = win->documentCount();
    for (int i = 0; i < count; ++i) {
        IDocument *doc = nullptr;
        if (win->documentAt(i, &doc) && doc)
            snap.documents.append(snapshotDocument(doc));
    }
    // currentDocumentIndex is left at its -1 default here; the caller
    // (gatherSnapshot) fills it in from MainWindow::currentDocumentIndex()
    // since that's a MainWindow query, not something snapshotWindow's
    // per-document loop above touches.

    snap.windowGeometry = win->geometry();
    snap.isMaximized = win->isMaximized();
    snap.isFullScreen = win->isFullScreen();
    if (QScreen *screen = win->screen()) {
        snap.screenGeometry = screen->geometry();
        snap.screenAvailableGeometry = screen->availableGeometry();
        snap.screenDevicePixelRatio = screen->devicePixelRatio();
    }
    snap.documentViewportSize = win->currentDocumentViewportSize();

    return snap;
}

} // namespace

AppSnapshot gatherSnapshot(Application *app) {
    AppSnapshot snap;

    snap.versionString = QStringLiteral(TRAILER_VERSION_STRING);
    parseCommitInfo(snap.versionString, &snap.commitSha, &snap.dirtyWorkingTree);
    snap.generatedAtUtc =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    snap.buildType = buildTypeString();

    snap.qtVersion = QString::fromLatin1(qVersion());
    snap.osPrettyName = QSysInfo::prettyProductName();
    snap.cpuArchitecture = QSysInfo::currentCpuArchitecture();

    if (!app)
        return snap; // degrade gracefully: header + platform info still useful

    Settings &settings = app->settings();
    snap.themeSetting = themeToString(settings.theme());
    snap.effectiveColorScheme =
        colorSchemeToString(QGuiApplication::styleHints()->colorScheme());
    snap.autoSaveEnabled = settings.autoSave();
    snap.openFilesIn = openFilesInToString(settings.openFilesIn());
    snap.recentFilesCount = static_cast<int>(app->recentFiles().entries().size());

    snap.mlRecognizeTextInBackground = settings.mlRecognizeTextInBackground();
    snap.mlPreloadSegmentationOnToolActivation =
        settings.mlPreloadSegmentationOnToolActivation();
    snap.mlRunOnBattery = settings.mlRunOnBattery();

    for (const ModelSpec &spec : app->modelRegistry().manifest()) {
        ModelSnapshot model;
        model.displayName = spec.displayName;
        model.available = app->modelRegistry().isAvailable(spec.id);
        model.neverDownload = ModelPolicy::isNeverDownload(app, spec.id);
        snap.models.append(model);
    }

    for (MainWindow *win : app->windows()) {
        if (!win)
            continue;
        WindowSnapshot winSnap = snapshotWindow(win);
        winSnap.sidebarVisible = win->isSidebarVisible();
        winSnap.markupToolbarVisible = win->isMarkupToolbarVisible();
        winSnap.formToolbarVisible = win->isFormToolbarVisible();
        winSnap.currentDocumentIndex = win->currentDocumentIndex();
        snap.windows.append(winSnap);
    }

    return snap;
}

namespace {

QString yesNo(bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); }

void appendDocumentLine(QString &out, const DocumentSnapshot &doc, int index,
                         bool includeFullPaths) {
    const QString name = (includeFullPaths && !doc.filePath.isEmpty())
                              ? doc.filePath
                              : doc.displayName;
    // No escaping here: this is plain/markdown text, not HTML — a prior
    // version called toHtmlEscaped() which corrupted filenames containing
    // '&'/'<'/'>' into literal "&amp;" etc. in the actual report.
    out += QStringLiteral("  %1. **%2**").arg(index + 1).arg(name);
    if (doc.dirty)
        out += QStringLiteral(" *(unsaved changes)*");
    if (doc.untitled)
        out += QStringLiteral(" *(untitled)*");
    out += QStringLiteral("\n");
    out += QStringLiteral("     - Type: %1").arg(documentTypeToString(doc.type));
    if (doc.pageCount > 0)
        out += QStringLiteral(", page %1 of %2").arg(doc.currentPage + 1).arg(doc.pageCount);
    out += QStringLiteral("\n");
    if (doc.supportsZoom) {
        out += QStringLiteral("     - Zoom: %1").arg(zoomModeToString(doc.zoomMode));
        if (doc.zoomMode == ZoomMode::Custom)
            out += QStringLiteral(" (%1%)").arg(qRound(doc.zoomFactor * 100));
        out += QStringLiteral("\n");
    }
    if (doc.supportsViewModes)
        out += QStringLiteral("     - View mode: %1\n").arg(viewModeToString(doc.viewMode));
    out += QStringLiteral("     - Has text layer: %1\n").arg(yesNo(doc.hasTextLayer));

    // Current-page text kind — see FeedbackReport.h for the full caveat on
    // why this can't distinguish visible native text from an invisible
    // OCR layer baked into the page by an external tool. Only rendered
    // for the two types that can carry page content at all.
    if (doc.type == DocumentType::Pdf || doc.type == DocumentType::Image) {
        out += QStringLiteral("     - Current page text: ");
        if (doc.currentPageHasExtractableText && doc.currentPageHasSelectableTextBlocks) {
            out += QStringLiteral(
                "extractable PDF text, ingested into Trailer's selection layer "
                "(NOTE: this cannot be told apart from an invisible OCR text "
                "layer baked into the page by an external tool — PDF text "
                "render mode isn't inspected — both read identically here)");
        } else if (!doc.currentPageHasExtractableText && doc.currentPageHasSelectableTextBlocks) {
            out += QStringLiteral(
                "from Trailer's own on-device OCR (no extractable native PDF "
                "text on this page)");
        } else if (doc.currentPageHasExtractableText && !doc.currentPageHasSelectableTextBlocks) {
            out += QStringLiteral(
                "extractable PDF text present, not yet ingested into Trailer's "
                "selection layer");
        } else {
            out += QStringLiteral("none detected on this page");
        }
        out += QStringLiteral("\n");
    }

    // Natural/intrinsic geometry — kept units-explicit (px for images, pt
    // for PDF pages) so a bare number can never be misread as the other.
    if (doc.type == DocumentType::Image) {
        out += QStringLiteral("     - Image size: ");
        if (!doc.imagePixelSize.isEmpty()) {
            out += QStringLiteral("%1 x %2 px")
                       .arg(doc.imagePixelSize.width())
                       .arg(doc.imagePixelSize.height());
            if (doc.imageSizePending) {
                out += QStringLiteral(" (from file header; full decode still pending)");
            } else if (doc.pageCount == 0) {
                // Header read succeeded (imagePixelSize is non-empty) but the
                // full pixel decode subsequently FAILED — ImageDocument::
                // imageAvailableOrPending() (and therefore pageCount()) goes
                // false once a finished-but-unsuccessful decode is adopted,
                // even though deviceSize() keeps reporting the header size.
                // Flag this explicitly rather than presenting a stale header
                // number as a confirmed, decoded size.
                out += QStringLiteral(" (from file header only — pixel decode failed)");
            }
        } else if (doc.imageSizePending) {
            out += QStringLiteral("not yet decoded");
        } else {
            out += QStringLiteral("unknown (image could not be decoded)");
        }
        out += QStringLiteral("\n");
        if (doc.imageDpr > 0.0)
            out += QStringLiteral("     - Image devicePixelRatio: %1\n").arg(doc.imageDpr);
    } else if (doc.type == DocumentType::Pdf && doc.pageCount > 0) {
        out += QStringLiteral("     - Page size: ");
        if (!doc.pdfPageSizePts.isEmpty()) {
            out += QStringLiteral("%1 x %2 pt (page %3)")
                       .arg(qRound(doc.pdfPageSizePts.width()))
                       .arg(qRound(doc.pdfPageSizePts.height()))
                       .arg(doc.currentPage + 1);
            if (doc.pdfPageSizeVariesByPage)
                out += QStringLiteral(" — differs from page 1's size");
        } else {
            out += QStringLiteral("unavailable");
        }
        out += QStringLiteral("\n");
    }
}

} // namespace

QString formatMarkdown(const AppSnapshot &snapshot, bool includeFullPaths) {
    QString out;

    // Stable, greppable header — a report pasted days later is
    // unambiguous about which build produced it.
    out += QStringLiteral("# %1 Diagnostic Report\n\n").arg(snapshot.appName);
    out += QStringLiteral("- **Version:** %1\n").arg(snapshot.versionString);
    out += QStringLiteral("- **Commit:** %1\n")
               .arg(snapshot.commitSha.isEmpty()
                        ? QStringLiteral("unknown (release build; see Version above)")
                        : (snapshot.commitSha +
                           (snapshot.dirtyWorkingTree ? QStringLiteral(" (dirty)") : QString())));
    out += QStringLiteral("- **Generated:** %1\n").arg(snapshot.generatedAtUtc);
    out += QStringLiteral("- **Build:** %1\n").arg(snapshot.buildType);
    out += QStringLiteral("- **Platform:** %1 (%2)\n")
               .arg(snapshot.osPrettyName, snapshot.cpuArchitecture);
    out += QStringLiteral("- **Qt:** %1\n").arg(snapshot.qtVersion);
    out += QStringLiteral("\n---\n\n");

    out += QStringLiteral("## App state\n\n");
    out += QStringLiteral("- Theme setting: %1 (effective: %2)\n")
               .arg(snapshot.themeSetting, snapshot.effectiveColorScheme);
    out += QStringLiteral("- Auto-save: %1\n").arg(yesNo(snapshot.autoSaveEnabled));
    out += QStringLiteral("- Open files in: %1\n").arg(snapshot.openFilesIn);
    out += QStringLiteral("- Recent files: %1\n").arg(snapshot.recentFilesCount);
    out += QStringLiteral("\n");

    out += QStringLiteral("## ML\n\n");
    out += QStringLiteral("- Recognize text in background: %1\n")
               .arg(yesNo(snapshot.mlRecognizeTextInBackground));
    out += QStringLiteral("- Preload segmentation on tool activation: %1\n")
               .arg(yesNo(snapshot.mlPreloadSegmentationOnToolActivation));
    out += QStringLiteral("- Run ML on battery: %1\n").arg(yesNo(snapshot.mlRunOnBattery));
    if (snapshot.models.isEmpty()) {
        out += QStringLiteral("- Models: (none registered)\n");
    } else {
        out += QStringLiteral("- Models:\n");
        for (const ModelSnapshot &model : snapshot.models) {
            out += QStringLiteral("  - %1: %2%3\n")
                       .arg(model.displayName, model.available
                                                    ? QStringLiteral("downloaded")
                                                    : QStringLiteral("not downloaded"),
                            model.neverDownload ? QStringLiteral(" (policy: never download)")
                                                 : QString());
        }
    }
    out += QStringLiteral("\n");

    out += QStringLiteral("## Windows and open documents\n\n");
    if (snapshot.windows.isEmpty()) {
        out += QStringLiteral("No windows open.\n\n");
    } else {
        for (int w = 0; w < snapshot.windows.size(); ++w) {
            const WindowSnapshot &win = snapshot.windows[w];
            out += QStringLiteral("### Window %1\n\n").arg(w + 1);
            // Window/screen geometry — the owner's real complaint ("it
            // chose a HUGE window size") needs both numbers to settle:
            // window size alone can't say whether it was actually
            // oversized relative to the screen it landed on. Omitted
            // (rather than printed as a misleading "0x0") when the
            // geometry could not be queried — see the QRect default in
            // FeedbackReport.h.
            if (win.windowGeometry.isValid()) {
                out += QStringLiteral("- Window geometry: %1x%2 at (%3, %4)%5\n")
                           .arg(win.windowGeometry.width())
                           .arg(win.windowGeometry.height())
                           .arg(win.windowGeometry.x())
                           .arg(win.windowGeometry.y())
                           .arg(win.isFullScreen
                                    ? QStringLiteral(" [fullscreen]")
                                    : (win.isMaximized ? QStringLiteral(" [maximized]")
                                                        : QString()));
            }
            if (win.screenGeometry.isValid()) {
                out += QStringLiteral(
                           "- Screen geometry: %1x%2 (available %3x%4), "
                           "devicePixelRatio %5\n")
                           .arg(win.screenGeometry.width())
                           .arg(win.screenGeometry.height())
                           .arg(win.screenAvailableGeometry.width())
                           .arg(win.screenAvailableGeometry.height())
                           .arg(win.screenDevicePixelRatio);
            }
            if (!win.documentViewportSize.isEmpty()) {
                out += QStringLiteral("- Document viewport size: %1 x %2 px\n")
                           .arg(win.documentViewportSize.width())
                           .arg(win.documentViewportSize.height());
            }
            out += QStringLiteral("- Sidebar visible: %1\n").arg(yesNo(win.sidebarVisible));
            out += QStringLiteral("- Markup toolbar visible: %1\n")
                       .arg(yesNo(win.markupToolbarVisible));
            out += QStringLiteral("- Form toolbar visible: %1\n")
                       .arg(yesNo(win.formToolbarVisible));
            if (win.documents.isEmpty()) {
                out += QStringLiteral("- No documents open (empty state)\n\n");
                continue;
            }
            out += QStringLiteral("- Documents (%1, current index %2):\n")
                       .arg(win.documents.size())
                       .arg(win.currentDocumentIndex);
            for (int d = 0; d < win.documents.size(); ++d)
                appendDocumentLine(out, win.documents[d], d, includeFullPaths);
            out += QStringLiteral("\n");
        }
    }

    if (!includeFullPaths) {
        out += QStringLiteral(
            "*(Full file paths omitted — enable \"Include full file paths\" in the "
            "Feedback Report dialog to include them.)*\n");
    }

    return out;
}

} // namespace trailer::feedback
