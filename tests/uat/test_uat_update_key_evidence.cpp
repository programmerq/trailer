// UAT harness — the two user-visible surfaces that change when a build
// has no update-signing key (see
// docs/decision-records/2026-08-02-update-pubkey-from-signing-secret.md):
//
//   1. Help ▸ Check for Updates…  — present but DISABLED, with a tooltip
//      saying why and where to go (G3).
//   2. Preferences ▸ Updates      — the status line explains the build
//      has no key; the action button keeps its place and label and is
//      disabled with a tooltip (G3 + G10: nothing moves).
//
// Both states are a compile-time property of the binary
// (Update::kUpdateChannelProvisioned), so this file asserts whichever
// state the build it was compiled into actually has, and each slot works
// in both. Run the SAME binary from an unprovisioned build tree and from
// one configured with -DTRAILER_UPDATE_PUBKEY=<hex> to produce the G2
// pair.
//
// When TRAILER_UPDATE_KEY_EVIDENCE_DIR is set it also writes curated
// offscreen grab() PNGs, matching the TRAILER_PREF_EVIDENCE_DIR /
// TRAILER_DEFERENCE_EVIDENCE_DIR convention used elsewhere in this suite
// (no env var set => no files written, no assertions skipped).
//
// Mirrors the custom-main + HOME-sandbox scaffolding of
// test_uat_preferences.cpp.

#include "UpdatePublicKey.h"
#include "app/Application.h"
#include "settings/Settings.h"
#include "ui/MainWindow.h"
#include "ui/PreferencesDialog.h"
#include "update/UpdateManager.h"

#include <QAction>
#include <QAbstractButton>
#include <QDir>
#include <QMenu>
#include <QLabel>
#include <QLayout>
#include <QMenuBar>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
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

QString evidenceDir() {
    const QByteArray dir = qgetenv("TRAILER_UPDATE_KEY_EVIDENCE_DIR");
    if (dir.isEmpty())
        return QString();
    const QString path = QString::fromLocal8Bit(dir);
    QDir().mkpath(path);
    return path;
}

// Filenames carry the build's own state so the two runs that make up the
// G2 pair cannot overwrite each other when pointed at one directory.
QString variant() {
    return Update::kUpdateChannelProvisioned ? QStringLiteral("with-key")
                                             : QStringLiteral("no-key");
}

QAction *findCheckForUpdates(MainWindow *mw) {
    for (QAction *top : mw->menuBar()->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *a : menu->actions()) {
            if (a->objectName() == QStringLiteral("action.help.checkForUpdates"))
                return a;
        }
    }
    return nullptr;
}

} // namespace

class TestUatUpdateKeyEvidence : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_helpMenuCheckForUpdatesMatchesProvisioning();
    void uat_preferencesUpdatesPaneMatchesProvisioning();
    void uat_updatesButtonDoesNotMoveAsStatusTextChanges();
};

void TestUatUpdateKeyEvidence::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-UPD-001 — the menu item exists in BOTH states (never removed, so
// nothing below it shifts: G10) and is enabled exactly when this build
// can actually verify an update (G3).
void TestUatUpdateKeyEvidence::uat_helpMenuCheckForUpdatesMatchesProvisioning() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString pdfPath = scratch.filePath(QStringLiteral("doc.pdf"));
    {
        QPdfWriter writer(pdfPath);
        writer.setPageSize(QPageSize(QPageSize::Letter));
        QPainter p(&writer);
        p.drawText(QRect(400, 400, 6000, 1200), Qt::AlignLeft, QStringLiteral("Update key evidence"));
        p.end();
    }
    app->openFiles({pdfPath});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1000, 700);
    mw->show();
    QApplication::processEvents();

    QAction *check = findCheckForUpdates(mw);
    QVERIFY2(check, "Help ▸ Check for Updates… must be PRESENT regardless of provisioning");
    QCOMPARE(check->isEnabled(), Update::kUpdateChannelProvisioned);

    if (!Update::kUpdateChannelProvisioned) {
        // G3: disabled is not enough — it must say why, and where to go.
        QVERIFY2(!check->toolTip().isEmpty(),
                 "A disabled Check for Updates… must carry a tooltip explaining why (G3)");
        QVERIFY(check->toolTip().contains(QStringLiteral("update-signing key")));
    }

    const QString dir = evidenceDir();
    if (dir.isEmpty())
        return;

    QMenu *help = nullptr;
    for (QAction *top : mw->menuBar()->actions()) {
        if (top->text() == QStringLiteral("&Help"))
            help = top->menu();
    }
    QVERIFY(help);
    help->popup(QPoint(0, 0));
    QApplication::processEvents();
    QVERIFY(help->grab().save(dir + QStringLiteral("/help-menu-%1.png").arg(variant())));
    help->close();
    QApplication::processEvents();
}

// UAT-UPD-002 — the Updates pane's action button keeps its place and its
// label in both states; only its enabled state and the status text
// differ (G10), and when disabled it explains itself (G3).
void TestUatUpdateKeyEvidence::uat_preferencesUpdatesPaneMatchesProvisioning() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    PreferencesDialog dlg(app->settings());
    dlg.setUpdateManager(&app->updateManager());
    dlg.selectUpdatesTab();
    dlg.resize(560, 420);
    dlg.show();
    QApplication::processEvents();

    // The button is found by walking the dialog rather than by a testing
    // accessor so this stays honest about what a user can see.
    QAbstractButton *actionButton = nullptr;
    for (auto *b : dlg.findChildren<QAbstractButton *>()) {
        if (b->text() == QStringLiteral("Check Now")) {
            actionButton = b;
            break;
        }
    }
    QVERIFY2(actionButton, "Updates pane must show a 'Check Now' button in both states");
    QCOMPARE(actionButton->isEnabled(), Update::kUpdateChannelProvisioned);
    if (!Update::kUpdateChannelProvisioned)
        QVERIFY(!actionButton->toolTip().isEmpty());

    const QString dir = evidenceDir();
    if (!dir.isEmpty())
        QVERIFY(dlg.grab().save(dir + QStringLiteral("/prefs-updates-%1.png").arg(variant())));

    dlg.close();
    QApplication::processEvents();
}

// UAT-UPD-003 — G10 (spatial constancy), asserted as geometry rather
// than eyeballed from a screenshot, per that gate's evidence rule.
//
// The Updates pane's status line legitimately varies in height: one line
// for "Never checked.", two while a check is running ("Checking for
// updates…\nFetching: <url>"), three for the no-signing-key message at
// this dialog's width. Before the status label reserved its tallest
// height, the Check Now button rode up and down with it — sliding out
// from under the pointer at the exact moment the user clicked it. This
// pins the button's position across every text length the pane can show.
void TestUatUpdateKeyEvidence::uat_updatesButtonDoesNotMoveAsStatusTextChanges() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    PreferencesDialog dlg(app->settings());
    dlg.setUpdateManager(&app->updateManager());
    dlg.selectUpdatesTab();
    dlg.resize(560, 420);
    dlg.show();
    QApplication::processEvents();

    auto *label = dlg.findChild<QLabel *>(QStringLiteral("updatesStatusLabel"));
    QVERIFY(label);
    QAbstractButton *button = nullptr;
    for (auto *b : dlg.findChildren<QAbstractButton *>()) {
        if (b->text() == QStringLiteral("Check Now")) {
            button = b;
            break;
        }
    }
    QVERIFY(button);

    const QStringList statuses = {
        QStringLiteral("Never checked."),
        QStringLiteral("You're up to date."),
        QStringLiteral("Checking for updates…\nFetching: https://example.invalid/appcast-nightly.json"),
        QStringLiteral("This build has no update-signing key, so it can't verify an update.\n"
                       "Official builds update automatically — see the Releases page."),
    };

    // Checked at the pane's default width AND at the narrowest the user
    // can drag the dialog: the reserved height is a line COUNT, so a
    // narrow enough dialog re-wraps the longest message onto an extra
    // line and the reservation stops covering it. Measuring at the
    // minimum is what makes the guarantee hold everywhere in between
    // rather than only at the size this test happened to pick.
    const int minWidth = dlg.minimumSizeHint().width();
    for (int width : {560, minWidth}) {
        dlg.resize(width, 420);
        dlg.layout()->activate();
        QApplication::processEvents();

        QPoint reference;
        for (int i = 0; i < statuses.size(); ++i) {
            label->setText(statuses.at(i));
            dlg.layout()->activate();
            QApplication::processEvents();
            const QPoint pos = button->mapTo(&dlg, QPoint(0, 0));
            if (i == 0) {
                reference = pos;
            } else {
                QVERIFY2(pos == reference,
                         qPrintable(QStringLiteral(
                             "Check Now moved from y=%1 to y=%2 at width %3 when the status "
                             "text changed to \"%4\" (G10)")
                                        .arg(reference.y())
                                        .arg(pos.y())
                                        .arg(width)
                                        .arg(statuses.at(i).left(40))));
            }
        }
    }
}

int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatUpdateKeyEvidence tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_update_key_evidence.moc"
