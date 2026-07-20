#include "ui/FileChangeBanner.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

using namespace trailer;

// Tests for the in-window file-change banner: mode transitions, the G3
// disabled-Compare placeholder, and the button→signal wiring.
class TestFileChangeBanner : public QObject {
    Q_OBJECT

  private slots:
    void startsHidden() {
        FileChangeBanner banner;
        QCOMPARE(banner.mode(), FileChangeBanner::Mode::Hidden);
    }

    // Conflict mode exposes Reload + Keep mine, and Compare is present but
    // DISABLED with an explanatory tooltip (G3: no lying controls).
    void conflictModeOffersReloadKeepAndDisabledCompare() {
        FileChangeBanner banner;
        banner.showConflict();
        QCOMPARE(banner.mode(), FileChangeBanner::Mode::Conflict);
        QVERIFY(!banner.messageText().isEmpty());
        QVERIFY(banner.reloadEnabled());
        QVERIFY(banner.keepMineEnabled());
        QVERIFY(!banner.compareEnabled());
        QVERIFY(!banner.compareTooltip().isEmpty());
        QVERIFY(!banner.saveEnabled()); // Save is a Deleted-mode control
    }

    // Deleted mode swaps to a Save affordance and hides the conflict buttons.
    void deletedModeOffersSave() {
        FileChangeBanner banner;
        banner.showDeleted();
        QCOMPARE(banner.mode(), FileChangeBanner::Mode::Deleted);
        QVERIFY(!banner.messageText().isEmpty());
        QVERIFY(banner.saveEnabled());
        QVERIFY(!banner.reloadEnabled());
        QVERIFY(!banner.keepMineEnabled());
    }

    void reloadButtonEmitsSignal() {
        FileChangeBanner banner;
        banner.showConflict();
        QSignalSpy spy(&banner, &FileChangeBanner::reloadRequested);
        banner.clickReloadForTest();
        QCOMPARE(spy.count(), 1);
    }

    void keepMineButtonEmitsSignal() {
        FileChangeBanner banner;
        banner.showConflict();
        QSignalSpy spy(&banner, &FileChangeBanner::keepMineRequested);
        banner.clickKeepMineForTest();
        QCOMPARE(spy.count(), 1);
    }

    void saveButtonEmitsSignal() {
        FileChangeBanner banner;
        banner.showDeleted();
        QSignalSpy spy(&banner, &FileChangeBanner::saveRequested);
        banner.clickSaveForTest();
        QCOMPARE(spy.count(), 1);
    }

    void dismissHidesAndSignals() {
        FileChangeBanner banner;
        banner.showConflict();
        QSignalSpy spy(&banner, &FileChangeBanner::dismissed);
        banner.dismiss();
        QCOMPARE(banner.mode(), FileChangeBanner::Mode::Hidden);
        // dismiss() itself doesn't emit; only the Dismiss button does. Assert
        // the mode reset is the observable effect.
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestFileChangeBanner)
#include "test_file_change_banner.moc"
