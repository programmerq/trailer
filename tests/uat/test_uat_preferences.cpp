// UAT harness — Preferences pane: live Theme control + tabs-grow-with-settings
//
// Drives PreferencesDialog (and, for the evidence shots, Application +
// MainWindow) in-process under QT_QPA_PLATFORM=offscreen to lock two
// polish-backlog rules:
//
//   • Theme live-wire (backlog 2026-07-12-theme-live-wire): the Theme
//     control is ENABLED (no longer shown-but-disabled), and the obsolete
//     "Not applied yet" helper label is gone. The live-apply path itself
//     (colour scheme + icon re-tint) is asserted in the unit tier
//     (tests/test_settings.cpp::colorSchemeMapping,
//     tests/test_preferences.cpp::themeAppliesLiveThroughSignal); here we
//     guard the surfaced control state.
//   • Tabs grow with settings (backlog 2026-07-12-preferences-tabs-grow-
//     with-settings): every VISIBLE Preferences tab carries at least one
//     enabled, operable control — no empty/stub tabs (e.g. a Forms tab is
//     not shown until a Forms setting is wired). The dialog already
//     conforms by construction; this test is the regression lock so a
//     future empty addTab() fails CI.
//
// When TRAILER_PREF_EVIDENCE_DIR is set, the harness also writes curated
// offscreen grab() PNGs (the G2 evidence): the Preferences General tab with
// the enabled Theme combo in light and dark, and the running app in light
// (the default appearance) and dark (the new capability) — the same window
// and document in both, i.e. the theme before/after pair.
//
// Mirrors the custom-main + HOME-sandbox scaffolding of
// test_uat_zoom_indicator.cpp so Settings/RecentFiles write into a throwaway
// sandbox.

#include "app/Application.h"
#include "settings/Settings.h"
#include "ui/MainWindow.h"
#include "ui/PreferencesDialog.h"
#include "ui/TwoPageView.h"
#include "UpdatePublicKey.h"
#include "update/UpdateManager.h"
#include "util/DocumentSurroundColor.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPageSize>
#include <QPainter>
#include <QPalette>
#include <QPdfView>
#include <QPdfWriter>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyleHints>
#include <QTabWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QString writeStaticImage(const QString &path) {
    QImage img(320, 240, QImage::Format_ARGB32);
    img.fill(qRgb(210, 216, 224));
    img.save(path, "PNG");
    return path;
}

// True if `page` hosts at least one enabled, user-operable control. We
// count the standard interactive widget kinds (combo / check / spin / any
// push- or tool-button). A tab with none of these enabled is an empty /
// stub tab — exactly what the tabs-grow-with-settings rule forbids.
bool hasEnabledOperableControl(const QWidget *page) {
    const auto combos = page->findChildren<QComboBox *>();
    for (auto *c : combos)
        if (c->isEnabled())
            return true;
    const auto checks = page->findChildren<QCheckBox *>();
    for (auto *c : checks)
        if (c->isEnabled())
            return true;
    const auto spins = page->findChildren<QSpinBox *>();
    for (auto *c : spins)
        if (c->isEnabled())
            return true;
    const auto buttons = page->findChildren<QAbstractButton *>();
    for (auto *b : buttons)
        if (b->isEnabled())
            return true;
    return false;
}

// A representative dark palette, equivalent to what a platform theme
// (macOS / Windows / a Linux Qt platform-theme plugin) supplies when
// QStyleHints::setColorScheme(Dark) flips the appearance. The offscreen
// QPA plugin used for UAT has NO platform theme, so setColorScheme cannot
// derive a dark palette on its own (light and dark grabs come out
// byte-identical). For the evidence shots we apply this palette explicitly
// so the offscreen grab genuinely reaches the dark-rendered state real
// users see — and, critically, so the themed-icon re-tint (the actual code
// under test) has a dark palette to tint against. This is documented in the
// PR as the sanctioned offscreen fallback for a state that cannot otherwise
// be reached (AGENTS.md G2 capture-method note).
QPalette darkPalette() {
    QPalette p;
    const QColor window(0x2b, 0x2b, 0x2e);
    const QColor base(0x1e, 0x1e, 0x21);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor disabled(0x7a, 0x7a, 0x7a);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, QColor(0x3d, 0x6e, 0xb4));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

// The evidence output directory, or empty when evidence emission is off.
QString evidenceDir() {
    const QByteArray dir = qgetenv("TRAILER_PREF_EVIDENCE_DIR");
    if (dir.isEmpty())
        return QString();
    const QString path = QString::fromLocal8Bit(dir);
    QDir().mkpath(path);
    return path;
}

} // namespace

class TestUatPreferences : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_pref_010_themeControlEnabledAndHelperLabelGone();
    void uat_pref_020_everyVisibleTabHasEnabledOperableControl();
    void uat_pref_030_updatesTabReflectsManagerState();
    void uat_xct_005_documentSurroundColourFollowsPaletteLive();
    void uat_pref_090_evidenceShots();

  private:
    QTemporaryDir m_scratch;
};

void TestUatPreferences::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// Theme live-wire: the control is enabled and the old disabled-state helper
// label ("Not applied yet — planned for a future release.") is gone.
void TestUatPreferences::uat_pref_010_themeControlEnabledAndHelperLabelGone() {
    QVERIFY(m_scratch.isValid());
    Settings s(m_scratch.filePath(QStringLiteral("pref010.toml")));
    PreferencesDialog dlg(s);

    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("themeCombo"));
    QVERIFY2(combo, "Preferences must host a themeCombo");
    QVERIFY2(combo->isEnabled(), "The Theme control must be enabled (live-wired), not disabled");
    QVERIFY2(!dlg.findChild<QLabel *>(QStringLiteral("themeHelpLabel")),
             "The obsolete 'not applied yet' helper label must be gone");
    QCOMPARE(combo->count(), 3); // System / Light / Dark
}

// Tabs grow with settings: every visible tab has >=1 enabled operable
// control. The dialog is built exactly as the app builds it (both injected
// callbacks wired), then every tab page is inspected. A future empty
// addTab() (e.g. a premature Forms tab with no wired setting) fails here.
void TestUatPreferences::uat_pref_020_everyVisibleTabHasEnabledOperableControl() {
    QVERIFY(m_scratch.isValid());
    Settings s(m_scratch.filePath(QStringLiteral("pref020.toml")));
    PreferencesDialog dlg(s);
    // Mirror the shipped wiring so the ML/Advanced action buttons are live.
    dlg.setManageModelsCallback([]() {});
    dlg.setResetAllCallback([]() {});

    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
    QVERIFY2(tabs, "Preferences must host a tabWidget");
    QVERIFY2(tabs->count() > 0, "Preferences must have at least one tab");

    for (int i = 0; i < tabs->count(); ++i) {
        QWidget *page = tabs->widget(i);
        QVERIFY(page);
        const QString label = tabs->tabText(i);
        QVERIFY2(hasEnabledOperableControl(page),
                 qPrintable(QStringLiteral("Preferences tab '%1' has no enabled operable "
                                           "control — empty/stub tabs are forbidden "
                                           "(tabs grow with settings)")
                                .arg(label)));
    }
}

// Updates pane (docs/decision-records/2026-07-30-nightly-auto-update-
// channel.md): the status label + action button text/enabled-state track
// UpdateManager's state directly — this is the regression lock for that
// wiring, independent of the curated G2 screenshots below. Uses
// debugForceStateForTesting (a documented test-only seam on UpdateManager)
// so every state is reachable without a real network round-trip.
void TestUatPreferences::uat_pref_030_updatesTabReflectsManagerState() {
    QVERIFY(m_scratch.isValid());
    Settings s(m_scratch.filePath(QStringLiteral("pref030.toml")));
    Update::UpdateManager mgr(s);
    PreferencesDialog dlg(s);
    dlg.setUpdateManager(&mgr);

    auto *status = dlg.findChild<QLabel *>(QStringLiteral("updatesStatusLabel"));
    auto *action = dlg.findChild<QPushButton *>(QStringLiteral("updatesActionButton"));
    QVERIFY2(status, "Preferences must host an updatesStatusLabel");
    QVERIFY2(action, "Preferences must host an updatesActionButton");

    // Idle (never checked): action button reads "Check Now", and is
    // enabled iff this build actually carries an update-signing key.
    // A build configured without -DTRAILER_UPDATE_PUBKEY (every ordinary
    // local build, PR, and fork — see cmake/UpdatePublicKey.h.in) can
    // never verify a feed, so the button is disabled with a tooltip
    // rather than offering a check that can only fail (G3). The label and
    // position are identical either way, which is what the rest of this
    // slot and UAT-UPD-003 pin.
    QCOMPARE(action->isEnabled(), Update::kUpdateChannelProvisioned);
    QCOMPARE(action->text(), QStringLiteral("Check Now"));
    if (!Update::kUpdateChannelProvisioned) {
        QVERIFY2(!action->toolTip().isEmpty(),
                 "A disabled Check Now must say why (G3)");
        // The remaining state transitions below all route through
        // refreshUpdatesStatus()'s no-key early return in this
        // configuration, so they cannot be exercised here.
        return;
    }

    Update::FeedEntry entry;
    entry.tag = QStringLiteral("nightly-20260730");
    entry.buildNumber = 4821;

    mgr.debugForceStateForTesting(Update::UpdateManager::State::Checking);
    QVERIFY(!action->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("Checking")));

    mgr.debugForceStateForTesting(Update::UpdateManager::State::UpdateAvailable, entry);
    QVERIFY(action->isEnabled());
    QVERIFY(status->text().contains(entry.tag));
    QVERIFY(action->text().contains(QStringLiteral("Download")));

    mgr.debugForceStateForTesting(Update::UpdateManager::State::UpToDate);
    QVERIFY(action->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("up to date"), Qt::CaseInsensitive));

    mgr.debugForceStateForTesting(Update::UpdateManager::State::Error, {},
                                  QStringLiteral("Could not reach GitHub: offline"));
    QVERIFY(action->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("offline")));
}

// UAT-XCT-005 — PdfDocument's QPdfView and TwoPageView both derive their
// document-surround colour from the ONE shared rule,
// trailer::documentSurroundColor() (util/DocumentSurroundColor.h), instead
// of two independently-hand-picked palette roles that can drift apart —
// and that derivation is LIVE (PR #105), re-run on a runtime theme change,
// not fixed at open time. See DR
// 2026-07-31-document-surround-colour-follows-base.
//
// documentSurroundColor() is intentionally NOT "always ::Base": it prefers
// ::Dark (a visibly recessed canvas behind a typically-white page — the
// convention every mainstream PDF viewer follows) and only falls back to
// ::Base when ::Dark would resolve LIGHTER than it. In Trailer's real
// stock light palette ::Dark (#9f9f9f) is already darker than ::Base
// (#ffffff), so light mode is a deliberate NO-OP for this fix — this test
// locks that as an invariant, not just an implementation detail: an
// earlier draft of this fix used ::Base unconditionally and made a light-
// mode PDF page invisible against its own canvas (caught by the
// pre-existing uat_vwr_079_zoomReadoutMatchesRenderScale, which measures a
// page by scanning for contrast against the canvas and depends on that
// contrast existing). Dark mode is where ::Dark can end up inverted
// (Trailer's dark palette is synthesized entirely by
// QStyleHints::setColorScheme, no hand-built dark QPalette of our own) —
// this test constructs that inversion deliberately (rather than hoping
// Qt's own dark-palette synthesis reproduces it, which the offscreen QPA
// plugin can't derive on its own — see darkPalette()'s comment above) so
// the fallback-to-::Base branch is exercised for real, proving the PDF
// surround self-heals to match the image viewer's ::Base exactly in
// precisely the case that was reported broken.
void TestUatPreferences::uat_xct_005_documentSurroundColourFollowsPaletteLive() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QPalette lightPalette = qApp->palette();

    QPdfWriter writer(m_scratch.filePath(QStringLiteral("xct005.pdf")));
    writer.setPageSize(QPageSize(QPageSize::A4));
    {
        QPainter p(&writer);
        p.drawText(72, 72, QStringLiteral("UAT-XCT-005 fixture"));
    }
    const QString pdfPath = m_scratch.filePath(QStringLiteral("xct005.pdf"));
    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("xct005.png")));
    // A second PDF, opened as a second tab, so the multi-document loop in
    // MainWindow::refreshThemedIcons() (every open document in the window,
    // not just the current tab) is genuinely exercised below — not just
    // the single-document case.
    QPdfWriter writer2(m_scratch.filePath(QStringLiteral("xct005b.pdf")));
    writer2.setPageSize(QPageSize(QPageSize::A4));
    {
        QPainter p2(&writer2);
        p2.drawText(72, 72, QStringLiteral("UAT-XCT-005 second-tab fixture"));
    }
    const QString pdfPath2 = m_scratch.filePath(QStringLiteral("xct005b.pdf"));

    // Force all documents into ONE window (default OpenFilesIn is
    // NewWindow for a mixed PDF+image batch — isImageBatch() only
    // special-cases an all-image batch, Application.cpp) so both views are
    // reachable from the same MainWindow via findChild below. Restored on
    // the way out so this test doesn't leak state into a later slot.
    const OpenFilesIn priorOpenFilesIn = app->settings().openFilesIn();
    app->settings().setOpenFilesIn(OpenFilesIn::SameWindow);
    auto restoreOpenFilesIn =
        qScopeGuard([&] { app->settings().setOpenFilesIn(priorOpenFilesIn); });

    app->openFiles({pdfPath, imgPath, pdfPath2});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    // findChildren (plural) for QPdfView — there are TWO open PDF tabs, and
    // the multi-document loop in MainWindow::refreshThemedIcons() (every
    // open document in the window, not just the current tab) needs BOTH
    // exercised, not just the first one findChild would return.
    const QList<QPdfView *> pdfViews = mw->findChildren<QPdfView *>();
    QVERIFY2(pdfViews.size() == 2,
             qPrintable(QStringLiteral("expected 2 QPdfView instances (one per PDF tab), found %1")
                            .arg(pdfViews.size())));
    auto *scroll = mw->findChild<QScrollArea *>();
    QVERIFY2(scroll, "image scroll area not found — did ImageDocument::createView change?");
    auto *twoPage = mw->findChild<TwoPageView *>();
    QVERIFY2(twoPage, "TwoPageView not found — PdfDocument builds it eagerly alongside QPdfView");

    // Every PDF-shaped surface — BOTH tabs' QPdfViews, and TwoPageView —
    // must apply the SAME shared-rule output as the app's current palette,
    // whatever it is: the "one source of truth, can't drift apart"
    // invariant, tested by calling the real rule as the oracle rather than
    // duplicating its logic by hand, and across every open tab rather than
    // only the current one.
    //
    // QPdfView is checked via its palette PROPERTY directly — PdfDocument
    // pins ::Dark to the rule's output via setPalette() (applyViewPalette),
    // so the stored property IS the tested value, no paint needed.
    //
    // TwoPageView is checked by GRABBING its viewport and sampling a corner
    // pixel instead — unlike QPdfView it pins nothing; paintEvent() calls
    // documentSurroundColor() fresh against viewport()->palette() on every
    // repaint (TwoPageView.cpp), so there is no stored property to read and
    // the only faithful test is the actual painted output. (0, 0) sits
    // inside kOuterMargin regardless of scroll position or whether a spread
    // is loaded, so it is always canvas, never page content.
    auto checkBothMatchSharedRule = [&]() {
        const QColor expected = documentSurroundColor(qApp->palette());
        for (QPdfView *pv : pdfViews)
            QCOMPARE(pv->palette().color(QPalette::Dark), expected);

        twoPage->viewport()->update();
        QApplication::processEvents();
        const QImage grabbed = twoPage->viewport()->grab().toImage();
        QVERIFY2(!grabbed.isNull(), "TwoPageView viewport grab failed");
        // .rgb(), not a bare QColor QCOMPARE — QColor::operator== also
        // compares the internal colour SPEC (Rgb / ExtendedRgb / ...), so a
        // pixel sampled from a QImage can print the identical hex as a
        // QPalette-derived QColor and still fail == on spec alone.
        QCOMPARE(grabbed.pixelColor(0, 0).rgb(), expected.rgb());
    };

    // --- Light theme: the REAL, un-doctored default palette. ---
    app->applyTheme(Theme::Light);
    QApplication::processEvents();
    checkBothMatchSharedRule();
    const QColor pdfSurroundLight = pdfViews.first()->palette().color(QPalette::Dark);
    // The fix must NOT touch the already-correct light-mode appearance:
    // the canvas stays darker than a white page (Dark genuinely IS darker
    // than Base in Trailer's stock light palette), never matching Base
    // outright the way an unconditional-::Base fix would have.
    QVERIFY2(pdfSurroundLight.lightness() <
                 scroll->palette().color(QPalette::Base).lightness(),
             "light-mode PDF/TwoPage canvas must stay darker than a white "
             "page (the pre-existing, un-complained-about appearance) — "
             "not collapse to the same white as the page itself");

    // --- Dark theme, with ::Dark deliberately synthesized LIGHTER than
    // ::Base — reproducing the reported bug on purpose, rather than hoping
    // Qt's own (here, undeliverable) dark-palette synthesis happens to
    // invert them. ---
    QPalette invertedDarkPalette = darkPalette();
    invertedDarkPalette.setColor(QPalette::Dark, QColor(0xd0, 0xd0, 0xd4));
    qApp->setPalette(invertedDarkPalette);
    app->applyTheme(Theme::Dark);
    QApplication::processEvents();
    checkBothMatchSharedRule();
    const QColor pdfSurroundDark = pdfViews.first()->palette().color(QPalette::Dark);
    // The self-heal, made explicit: with ::Dark inverted, the PDF surround
    // now matches the image viewer's ::Base EXACTLY — "make the PDF
    // surround match the image one," precisely in the case it was wrong.
    QCOMPARE(pdfSurroundDark, scroll->palette().color(QPalette::Base));

    // Prove this is a LIVE re-derivation on the SAME already-open document,
    // not a construction-time value.
    QVERIFY2(pdfSurroundLight != pdfSurroundDark,
             "the theme flip did not actually change the surround colour — "
             "either the palette swap or the refresh path is not wired");

    // Restore the ambient light palette / scheme for any later slot.
    qApp->setPalette(lightPalette);
    app->applyTheme(Theme::System);
    QApplication::processEvents();
}

// Curated G2 evidence (only when TRAILER_PREF_EVIDENCE_DIR is set): the
// Preferences General tab with the enabled Theme combo in light and dark,
// and the running app in light (default appearance) and dark (new
// capability) — same window and document, i.e. the theme before/after pair.
void TestUatPreferences::uat_pref_090_evidenceShots() {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        QSKIP("TRAILER_PREF_EVIDENCE_DIR not set; skipping evidence capture");

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QPalette lightPalette = qApp->palette();
    // Applies `theme` the way the app does (scheme + icon re-tint), and for
    // Dark also installs the representative dark palette the offscreen QPA
    // cannot derive, so the grab renders genuinely dark. applyTheme's icon
    // refresh runs AFTER the palette is in place, so icons tint correctly.
    const auto setTheme = [&](Theme theme) {
        if (theme == Theme::Dark)
            qApp->setPalette(darkPalette());
        else
            qApp->setPalette(lightPalette);
        app->applyTheme(theme);
        QApplication::processEvents();
    };

    // --- Preferences General tab, enabled Theme combo, light then dark ---
    {
        Settings s(m_scratch.filePath(QStringLiteral("pref090.toml")));
        PreferencesDialog dlg(s);
        dlg.setManageModelsCallback([]() {});
        dlg.setResetAllCallback([]() {});
        auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        QVERIFY(tabs);
        tabs->setCurrentIndex(0); // General
        dlg.resize(560, 360);
        dlg.show();

        setTheme(Theme::Light);
        QVERIFY(dlg.grab().save(dir + "/pref-general-after-light.png"));

        setTheme(Theme::Dark);
        QVERIFY(dlg.grab().save(dir + "/pref-general-after-dark.png"));
    }

    // --- Updates pane: idle / checking / update-available / up-to-date /
    // error (docs/decision-records/2026-07-30-nightly-auto-update-channel.md,
    // G2 evidence for the new Updates row in DESIGN §6.13). One dialog
    // instance, one UpdateManager driven through debugForceStateForTesting
    // so every state is reachable deterministically offscreen. ---
    {
        Settings s(m_scratch.filePath(QStringLiteral("pref090-updates.toml")));
        Update::UpdateManager mgr(s);
        PreferencesDialog dlg(s);
        dlg.setManageModelsCallback([]() {});
        dlg.setResetAllCallback([]() {});
        dlg.setUpdateManager(&mgr);
        dlg.selectUpdatesTab();
        dlg.resize(560, 360);
        dlg.show();
        setTheme(Theme::Light);

        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/pref-updates-idle.png"));

        mgr.debugForceStateForTesting(
            Update::UpdateManager::State::Checking, {}, {},
            QStringLiteral(
                "https://api.github.com/repos/programmerq/trailer/releases?per_page=10"));
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/pref-updates-checking.png"));

        Update::FeedEntry entry;
        entry.tag = QStringLiteral("nightly-20260730");
        entry.buildNumber = 4821;
        mgr.debugForceStateForTesting(Update::UpdateManager::State::UpdateAvailable, entry);
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/pref-updates-available.png"));

        mgr.debugForceStateForTesting(Update::UpdateManager::State::UpToDate);
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/pref-updates-uptodate.png"));

        mgr.debugForceStateForTesting(
            Update::UpdateManager::State::Error, {},
            QStringLiteral("No nightly release with a signed update feed was found."));
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/pref-updates-error.png"));
    }

    // --- Running app (same window + document) in light then dark ---
    {
        const QString imgPath =
            writeStaticImage(m_scratch.filePath(QStringLiteral("pref090-doc.png")));
        app->openFiles({imgPath});
        QApplication::processEvents();
        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        mw->resize(1000, 720);
        mw->show();
        QApplication::processEvents();

        setTheme(Theme::Light);
        QVERIFY(mw->grab().save(dir + "/app-theme-before-light.png"));

        setTheme(Theme::Dark);
        QVERIFY(mw->grab().save(dir + "/app-theme-after-dark.png"));
    }

    // Restore the ambient light palette / scheme for any later slot.
    qApp->setPalette(lightPalette);
    app->applyTheme(Theme::System);
    QApplication::processEvents();
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatPreferences tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_preferences.moc"
