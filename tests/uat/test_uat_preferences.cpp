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

#include <QAbstractButton>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPalette>
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
