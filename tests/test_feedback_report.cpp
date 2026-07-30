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

QTEST_MAIN(TestFeedbackReport)
#include "test_feedback_report.moc"
