#include "diagnostics/FeedbackReport.h"

#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;
using namespace trailer::feedback;

// Pure-formatting unit tests: build an AppSnapshot by hand (no
// Application, no QWidget, no display server needed) and assert on the
// rendered markdown. This is the "gathering vs formatting" split
// FeedbackReport.h documents — formatMarkdown() is the part with real
// branching logic and is fully testable this way.

namespace {

AppSnapshot minimalSnapshot() {
    AppSnapshot s;
    s.appName = QStringLiteral("Trailer");
    s.versionString = QStringLiteral("0.3.0-dev+42.gabc1234");
    s.commitSha = QStringLiteral("abc1234");
    s.dirtyWorkingTree = false;
    s.generatedAtUtc = QStringLiteral("2026-07-30T12:00:00Z");
    s.buildType = QStringLiteral("Release");
    s.qtVersion = QStringLiteral("6.11.0");
    s.osPrettyName = QStringLiteral("Ubuntu 24.04");
    s.cpuArchitecture = QStringLiteral("x86_64");
    s.themeSetting = QStringLiteral("System");
    s.effectiveColorScheme = QStringLiteral("Dark");
    s.autoSaveEnabled = true;
    s.openFilesIn = QStringLiteral("new_tab");
    s.recentFilesCount = 5;
    s.mlRecognizeTextInBackground = true;
    s.mlPreloadSegmentationOnToolActivation = true;
    s.mlRunOnBattery = false;
    return s;
}

} // namespace

class TestFeedbackReport : public QObject {
    Q_OBJECT
  private slots:
    void headerCarriesStableGreppableFields();
    void releaseBuildWithNoPlusSuffixReportsUnknownCommit();
    void dirtyWorkingTreeIsFlagged();
    void pathsOmittedByDefault();
    void pathsIncludedWhenRequested();
    void noWindowsStillProducesAReport();
    void emptyStateWindowIsDescribed();
    void modelPolicyAndAvailabilityAreRendered();
    void documentFieldsRoundTripIntoText();
    void imageDimensionsAndDprAreRenderedWithUnits();
    void pendingImageDecodeIsReportedHonestly();
    void undecodableImageIsReportedHonestlyNotAsZero();
    void failedPixelDecodeIsFlaggedNotPresentedAsSettled();
    void pdfPageSizeIsRenderedInPoints();
    void pdfPageSizeVarianceAcrossPagesIsFlagged();
    void windowAndScreenGeometryAreRendered();
    void invalidGeometryIsOmittedNotFaked();
    void imageDimensionsDoNotLeakPathWhenPathsOmitted();
    void currentPageTextNoneDetected();
    void currentPageTextFromTrailerOcr();
    void currentPageTextExtractableAndIngestedCarriesInvisibleCaveat();
    void currentPageTextExtractableNotYetIngested();
    void currentPageTextOmittedForNonTextBearingDocType();
    void currentPageTextFromTrailerOcrAppliesToImagesToo();
    void currentPageTextAbsentWithNoDocumentOpen();
};

void TestFeedbackReport::headerCarriesStableGreppableFields() {
    const AppSnapshot s = minimalSnapshot();
    const QString md = formatMarkdown(s);

    QVERIFY(md.contains(QStringLiteral("# Trailer Diagnostic Report")));
    QVERIFY(md.contains(QStringLiteral("**Version:** 0.3.0-dev+42.gabc1234")));
    QVERIFY(md.contains(QStringLiteral("**Commit:** abc1234")));
    QVERIFY(md.contains(QStringLiteral("**Generated:** 2026-07-30T12:00:00Z")));
    QVERIFY(md.contains(QStringLiteral("**Build:** Release")));
    QVERIFY(md.contains(QStringLiteral("Ubuntu 24.04")));
    QVERIFY(md.contains(QStringLiteral("x86_64")));
    QVERIFY(md.contains(QStringLiteral("**Qt:** 6.11.0")));
}

void TestFeedbackReport::releaseBuildWithNoPlusSuffixReportsUnknownCommit() {
    AppSnapshot s = minimalSnapshot();
    s.versionString = QStringLiteral("0.3.0"); // clean release tag, no +build metadata
    s.commitSha.clear();
    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("**Commit:** unknown (release build")));
}

void TestFeedbackReport::dirtyWorkingTreeIsFlagged() {
    AppSnapshot s = minimalSnapshot();
    s.dirtyWorkingTree = true;
    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("abc1234 (dirty)")));
}

void TestFeedbackReport::pathsOmittedByDefault() {
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("lease-addendum.pdf");
    d.filePath = QStringLiteral("/home/alice/Documents/private-client/lease-addendum.pdf");
    d.type = DocumentType::Pdf;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s); // default includeFullPaths = false
    QVERIFY(md.contains(QStringLiteral("lease-addendum.pdf")));
    QVERIFY(!md.contains(QStringLiteral("/home/alice")));
    QVERIFY(!md.contains(QStringLiteral("private-client")));
    // The omission itself is disclosed to the user, not silent.
    QVERIFY(md.contains(QStringLiteral("Full file paths omitted")));
}

void TestFeedbackReport::pathsIncludedWhenRequested() {
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("lease-addendum.pdf");
    d.filePath = QStringLiteral("/home/alice/Documents/private-client/lease-addendum.pdf");
    d.type = DocumentType::Pdf;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s, /*includeFullPaths=*/true);
    QVERIFY(md.contains(QStringLiteral("/home/alice/Documents/private-client/lease-addendum.pdf")));
    QVERIFY(!md.contains(QStringLiteral("Full file paths omitted")));
}

void TestFeedbackReport::noWindowsStillProducesAReport() {
    const AppSnapshot s = minimalSnapshot(); // no windows appended
    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("No windows open.")));
}

void TestFeedbackReport::emptyStateWindowIsDescribed() {
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w; // a real window, zero documents (Windows/Linux empty state)
    s.windows.append(w);
    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("No documents open (empty state)")));
}

void TestFeedbackReport::modelPolicyAndAvailabilityAreRendered() {
    AppSnapshot s = minimalSnapshot();
    ModelSnapshot m1;
    m1.displayName = QStringLiteral("U2NetP (background removal)");
    m1.available = true;
    m1.neverDownload = false;
    ModelSnapshot m2;
    m2.displayName = QStringLiteral("PP-OCRv3 detector");
    m2.available = false;
    m2.neverDownload = true;
    s.models = {m1, m2};

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("U2NetP (background removal): downloaded")));
    QVERIFY(md.contains(QStringLiteral("PP-OCRv3 detector: not downloaded (policy: never download)")));
}

void TestFeedbackReport::documentFieldsRoundTripIntoText() {
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    w.sidebarVisible = true;
    w.markupToolbarVisible = true;
    w.formToolbarVisible = false;
    w.currentDocumentIndex = 0;

    DocumentSnapshot d;
    d.displayName = QStringLiteral("scan.pdf");
    d.type = DocumentType::Pdf;
    d.dirty = true;
    d.pageCount = 12;
    d.currentPage = 3;
    d.supportsZoom = true;
    d.zoomMode = ZoomMode::Custom;
    d.zoomFactor = 1.5;
    d.supportsViewModes = true;
    d.viewMode = ViewMode::Continuous;
    d.hasTextLayer = true;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("scan.pdf")));
    QVERIFY(md.contains(QStringLiteral("(unsaved changes)")));
    QVERIFY(md.contains(QStringLiteral("PDF")));
    QVERIFY(md.contains(QStringLiteral("page 4 of 12")));
    QVERIFY(md.contains(QStringLiteral("Custom")));
    QVERIFY(md.contains(QStringLiteral("150%")));
    QVERIFY(md.contains(QStringLiteral("Continuous")));
    QVERIFY(md.contains(QStringLiteral("Has text layer: yes")));
    QVERIFY(md.contains(QStringLiteral("Sidebar visible: yes")));
    QVERIFY(md.contains(QStringLiteral("Markup toolbar visible: yes")));
    QVERIFY(md.contains(QStringLiteral("Form toolbar visible: no")));
}

void TestFeedbackReport::imageDimensionsAndDprAreRenderedWithUnits() {
    // The owner's real bug: a 504x375 JPEG opened at 80% zoom in an
    // oversized window. Reproduce that shape and assert the report now
    // carries every number that used to require a separate `mediainfo`
    // run.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("photo.jpg");
    d.type = DocumentType::Image;
    d.supportsZoom = true;
    d.zoomMode = ZoomMode::Custom;
    d.zoomFactor = 0.8;
    d.imagePixelSize = QSize(504, 375);
    d.imageSizePending = false;
    d.imageDpr = 2.0;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Image size: 504 x 375 px")));
    QVERIFY(md.contains(QStringLiteral("Image devicePixelRatio: 2")));
    QVERIFY(md.contains(QStringLiteral("Zoom: Custom (80%)")));
    // Units must be unambiguous — never a bare "504 x 375" a reader could
    // mistake for points.
    QVERIFY(!md.contains(QStringLiteral("504 x 375\n")));
}

void TestFeedbackReport::pendingImageDecodeIsReportedHonestly() {
    // ADR 0008 staged open: the async full-resolution decode hasn't
    // landed yet, but the file header already gave a size estimate.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("big-scan.png");
    d.type = DocumentType::Image;
    d.imagePixelSize = QSize(4000, 3000); // header-only estimate
    d.imageSizePending = true;
    d.imageDpr = 1.0;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Image size: 4000 x 3000 px")));
    QVERIFY(md.contains(QStringLiteral("full decode still pending")));
}

void TestFeedbackReport::undecodableImageIsReportedHonestlyNotAsZero() {
    // A corrupt/unreadable file: no header size, decode never started.
    // Must never print a misleading "0 x 0".
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("corrupt.png");
    d.type = DocumentType::Image;
    d.imagePixelSize = QSize(); // unknown
    d.imageSizePending = false;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Image size: unknown (image could not be decoded)")));
    QVERIFY(!md.contains(QStringLiteral("0 x 0")));
}

void TestFeedbackReport::failedPixelDecodeIsFlaggedNotPresentedAsSettled() {
    // A valid-header, corrupt-body file: ImageDocument's header read
    // succeeded (imagePixelSize is non-empty) but the async full-pixel
    // decode subsequently FAILED, so imageAvailableOrPending()/pageCount()
    // go false even though deviceSize() still reports the header size.
    // The report must not present that header number as a confirmed,
    // decoded result.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("truncated.jpg");
    d.type = DocumentType::Image;
    d.pageCount = 0; // imageAvailableOrPending() == false after the failed decode
    d.imagePixelSize = QSize(1920, 1080); // still the header-read estimate
    d.imageSizePending = false; // the decode DID finish — it just failed
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Image size: 1920 x 1080 px")));
    QVERIFY(md.contains(QStringLiteral("pixel decode failed")));
}

void TestFeedbackReport::pdfPageSizeIsRenderedInPoints() {
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("brief.pdf");
    d.type = DocumentType::Pdf;
    d.pageCount = 3;
    d.currentPage = 0;
    d.pdfPageSizePts = QSizeF(612.0, 792.0); // US Letter
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Page size: 612 x 792 pt (page 1)")));
    // Never print an ambiguous bare "612 x 792" — pt must be explicit.
    QVERIFY(!md.contains(QStringLiteral("612 x 792\n")));
}

void TestFeedbackReport::pdfPageSizeVarianceAcrossPagesIsFlagged() {
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("mixed-sizes.pdf");
    d.type = DocumentType::Pdf;
    d.pageCount = 2;
    d.currentPage = 1;
    d.pdfPageSizePts = QSizeF(792.0, 612.0); // landscape page 2
    d.pdfPageSizeVariesByPage = true;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Page size: 792 x 612 pt (page 2)")));
    QVERIFY(md.contains(QStringLiteral("differs from page 1's size")));
}

void TestFeedbackReport::windowAndScreenGeometryAreRendered() {
    // The owner's other complaint: "it chose a HUGE window size." The
    // report now carries both the window's geometry and the screen it
    // sits on, so a reader can tell whether that's actually oversized.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    w.windowGeometry = QRect(50, 60, 2400, 1800);
    w.isMaximized = false;
    w.isFullScreen = false;
    w.screenGeometry = QRect(0, 0, 2560, 1440);
    w.screenAvailableGeometry = QRect(0, 25, 2560, 1415);
    w.screenDevicePixelRatio = 2.0;
    w.documentViewportSize = QSize(2380, 1700);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Window geometry: 2400x1800 at (50, 60)")));
    QVERIFY(md.contains(
        QStringLiteral("Screen geometry: 2560x1440 (available 2560x1415), devicePixelRatio 2")));
    QVERIFY(md.contains(QStringLiteral("Document viewport size: 2380 x 1700 px")));
    QVERIFY(!md.contains(QStringLiteral("[maximized]")));
    QVERIFY(!md.contains(QStringLiteral("[fullscreen]")));
}

void TestFeedbackReport::invalidGeometryIsOmittedNotFaked() {
    // No windows.size()==0 test already covers the zero-windows case;
    // this covers a WindowSnapshot present but with geometry never
    // populated (e.g. an old snapshot round-tripped without the new
    // fields) — must omit the line, not print a fake "0x0 at (0, 0)".
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w; // windowGeometry/screenGeometry left default (invalid)
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(!md.contains(QStringLiteral("Window geometry:")));
    QVERIFY(!md.contains(QStringLiteral("Screen geometry:")));
    QVERIFY(!md.contains(QStringLiteral("Document viewport size:")));
}

void TestFeedbackReport::imageDimensionsDoNotLeakPathWhenPathsOmitted() {
    // Dimensions are not sensitive and belong in the default output, but
    // adding them must not reopen the path-privacy hole the existing
    // pathsOmittedByDefault test guards.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("photo.jpg");
    d.filePath = QStringLiteral("/home/alice/Documents/private-client/photo.jpg");
    d.type = DocumentType::Image;
    d.imagePixelSize = QSize(504, 375);
    d.imageDpr = 2.0;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s); // default includeFullPaths = false
    QVERIFY(md.contains(QStringLiteral("504 x 375 px")));
    QVERIFY(md.contains(QStringLiteral("Image devicePixelRatio: 2")));
    QVERIFY(!md.contains(QStringLiteral("/home/alice")));
    QVERIFY(!md.contains(QStringLiteral("private-client")));
}

void TestFeedbackReport::currentPageTextNoneDetected() {
    // Neither extractable PDF text nor selectable-text blocks: a bare
    // scan page nobody has OCR'd yet.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("scan-only.pdf");
    d.type = DocumentType::Pdf;
    d.pageCount = 1;
    d.currentPage = 0;
    d.currentPageHasExtractableText = false;
    d.currentPageHasSelectableTextBlocks = false;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral("Current page text: none detected on this page")));
}

void TestFeedbackReport::currentPageTextFromTrailerOcr() {
    // No extractable native PDF text, but the selection overlay has
    // blocks anyway — they can only have come from Trailer's own OCR.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("scanned-then-ocrd.pdf");
    d.type = DocumentType::Pdf;
    d.pageCount = 1;
    d.currentPage = 0;
    d.currentPageHasExtractableText = false;
    d.currentPageHasSelectableTextBlocks = true;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(
        QStringLiteral("Current page text: from Trailer's own on-device OCR")));
}

void TestFeedbackReport::currentPageTextExtractableAndIngestedCarriesInvisibleCaveat() {
    // The owner's actual bug shape: extractable text present AND already
    // ingested into the selection layer. This is the ambiguous case — it
    // covers both a normal born-digital page AND a scanned page with an
    // invisible OCR text layer baked in by an external tool, and the
    // report must say so rather than claim to know which.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("manual-page-12.pdf");
    d.type = DocumentType::Pdf;
    d.pageCount = 365;
    d.currentPage = 11;
    d.currentPageHasExtractableText = true;
    d.currentPageHasSelectableTextBlocks = true;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral(
        "Current page text: extractable PDF text, ingested into Trailer's "
        "selection layer")));
    QVERIFY(md.contains(QStringLiteral("cannot be told apart from an invisible OCR text")));
}

void TestFeedbackReport::currentPageTextExtractableNotYetIngested() {
    // Extractable PDF text exists but hasn't landed in the selection
    // overlay yet (the transient window before ingestNativeTextLayer runs).
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("just-opened.pdf");
    d.type = DocumentType::Pdf;
    d.pageCount = 1;
    d.currentPage = 0;
    d.currentPageHasExtractableText = true;
    d.currentPageHasSelectableTextBlocks = false;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(QStringLiteral(
        "Current page text: extractable PDF text present, not yet ingested "
        "into Trailer's selection layer")));
}

void TestFeedbackReport::currentPageTextOmittedForNonTextBearingDocType() {
    // Unknown/stub document type: no "Current page text" line at all —
    // matches the existing pattern of omitting Zoom/View-mode lines for
    // capabilities that don't apply, rather than printing a hollow
    // "none detected" for a type that was never asked the question.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("mystery-file");
    d.type = DocumentType::Unknown;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(!md.contains(QStringLiteral("Current page text:")));
}

void TestFeedbackReport::currentPageTextFromTrailerOcrAppliesToImagesToo() {
    // Image docs can never have PDF-native extractable text, so only
    // "none" or "from Trailer's own on-device OCR" are reachable for
    // them — verify the Image branch reaches the same OCR label as PDF.
    AppSnapshot s = minimalSnapshot();
    WindowSnapshot w;
    DocumentSnapshot d;
    d.displayName = QStringLiteral("receipt.jpg");
    d.type = DocumentType::Image;
    d.imagePixelSize = QSize(504, 375);
    d.currentPageHasExtractableText = false;
    d.currentPageHasSelectableTextBlocks = true;
    w.documents.append(d);
    s.windows.append(w);

    const QString md = formatMarkdown(s);
    QVERIFY(md.contains(
        QStringLiteral("Current page text: from Trailer's own on-device OCR")));
}

void TestFeedbackReport::currentPageTextAbsentWithNoDocumentOpen() {
    // The no-document case: zero windows, and a window with zero
    // documents (the Windows/Linux empty state). Neither path has a
    // DocumentSnapshot to derive a "Current page text" line from, so the
    // report must never emit that label at all — not even a hollow
    // "none detected" for a page that doesn't exist.
    {
        const AppSnapshot s = minimalSnapshot(); // no windows appended
        const QString md = formatMarkdown(s);
        QVERIFY(!md.contains(QStringLiteral("Current page text:")));
    }
    {
        AppSnapshot s = minimalSnapshot();
        WindowSnapshot w; // a real window, zero documents
        s.windows.append(w);
        const QString md = formatMarkdown(s);
        QVERIFY(!md.contains(QStringLiteral("Current page text:")));
    }
}

QTEST_MAIN(TestFeedbackReport)
#include "test_feedback_report.moc"
