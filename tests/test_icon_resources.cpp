// Verifies every SVG resource registered in trailer.qrc under
// /icons/actions/ loads and renders to a valid 18 px pixmap. This is
// the comprehensive companion to TestFormToolbar::
// everyActionHasARenderedIcon — the latter only covers what the form
// toolbar happens to use. If a future refactor renames a qrc alias,
// drops a registration, or breaks the Qt6::Svg link, this test
// surfaces the regression before users see a blank toolbar button.
//
// Filled siblings are also exercised: tools listed in
// kIconsWithFilledVariant must produce a distinct On-state pixmap
// (not the Off pixmap by fallback) so the "this tool is armed"
// visual signal continues to render.

#include "ui/IconHelper.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

class TestIconResources : public QObject {
    Q_OBJECT
private slots:
    // Pull the resource bundle in explicitly. trailer_core is a
    // static library; if no test-binary symbol references the qrc-
    // generated init .o (which happens when we only call free
    // functions in IconHelper), the linker drops it and every icon
    // resource reports "No such file or directory" at runtime.
    void initTestCase();
    void everyIconResourceLoads_data();
    void everyIconResourceLoads();
    void everyFilledSiblingDiffersFromOutline_data();
    void everyFilledSiblingDiffersFromOutline();
};

namespace {

// Master inventory of every icon resource registered under
// /icons/actions/ in resources/trailer.qrc. Kept in lockstep with the
// .qrc file — adding a new icon means updating both.
const QStringList kAllIcons = {
    QStringLiteral("tool-select"),
    QStringLiteral("tool-rectangle"),
    QStringLiteral("tool-ellipse"),
    QStringLiteral("tool-line"),
    QStringLiteral("tool-arrow"),
    QStringLiteral("tool-freehand"),
    QStringLiteral("tool-text"),
    QStringLiteral("tool-note"),
    QStringLiteral("tool-speech-bubble"),
    QStringLiteral("tool-highlight-shape"),
    QStringLiteral("tool-zoom-lens"),
    QStringLiteral("tool-highlight"),
    QStringLiteral("tool-underline"),
    QStringLiteral("tool-strikeout"),
    QStringLiteral("tool-redact"),
    QStringLiteral("tool-autofill"),
    QStringLiteral("tool-sign-here"),
    QStringLiteral("tool-checkmark"),
    QStringLiteral("tool-xmark"),
    QStringLiteral("view-zoom-in"),
    QStringLiteral("view-zoom-out"),
    QStringLiteral("view-zoom-actual"),
    QStringLiteral("view-fit-page"),
    QStringLiteral("view-fit-width"),
    QStringLiteral("page-rotate-left"),
    QStringLiteral("page-rotate-right"),
    QStringLiteral("panel-sidebar"),
    QStringLiteral("panel-markup"),
    QStringLiteral("panel-form"),
};

// Tools with an armed/active state per docs/icon-guidelines.md §3.6.
// Each entry must have a corresponding `<name>-filled.svg` registered.
const QStringList kIconsWithFilledVariant = {
    QStringLiteral("tool-rectangle"),
    QStringLiteral("tool-ellipse"),
    QStringLiteral("tool-line"),
    QStringLiteral("tool-arrow"),
    QStringLiteral("tool-note"),
    QStringLiteral("tool-speech-bubble"),
};

QString resourceFor(const QString& base) {
    return QStringLiteral(":/icons/actions/") + base + QStringLiteral(".svg");
}

}  // namespace

void TestIconResources::initTestCase() {
    Q_INIT_RESOURCE(trailer);
}

void TestIconResources::everyIconResourceLoads_data() {
    QTest::addColumn<QString>("name");
    for (const QString& n : kAllIcons) {
        QTest::newRow(qPrintable(n)) << n;
    }
}

void TestIconResources::everyIconResourceLoads() {
    QFETCH(QString, name);
    const QIcon icon = trailer::themedActionIcon(resourceFor(name), Qt::black);
    QVERIFY2(!icon.isNull(),
             qPrintable(QStringLiteral("icon failed to load: %1").arg(name)));

    // Render at the shipped 18 px size and confirm we got real pixels,
    // not a transparent placeholder. An empty render would mean the
    // SVG parser silently rejected the file (malformed XML, etc.).
    const QPixmap pm = icon.pixmap(QSize(18, 18));
    QCOMPARE(pm.size(), QSize(18, 18));
    const QImage img = pm.toImage();
    int opaque = 0;
    for (int y = 0; y < img.height() && opaque == 0; ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 0) {
                opaque = 1;
                break;
            }
        }
    }
    QVERIFY2(opaque > 0,
             qPrintable(QStringLiteral("icon rendered fully transparent: %1")
                            .arg(name)));
}

void TestIconResources::everyFilledSiblingDiffersFromOutline_data() {
    QTest::addColumn<QString>("name");
    for (const QString& n : kIconsWithFilledVariant) {
        QTest::newRow(qPrintable(n)) << n;
    }
}

void TestIconResources::everyFilledSiblingDiffersFromOutline() {
    QFETCH(QString, name);
    const QIcon icon = trailer::themedActionIcon(resourceFor(name), Qt::black);
    QVERIFY(!icon.isNull());

    const QImage off = icon.pixmap(QSize(36, 36), QIcon::Normal, QIcon::Off)
                           .toImage();
    const QImage on  = icon.pixmap(QSize(36, 36), QIcon::Normal, QIcon::On)
                           .toImage();
    QVERIFY2(off != on,
             qPrintable(QStringLiteral(
                 "On-state pixmap equals Off — `%1-filled.svg` did not "
                 "render or was not auto-paired.").arg(name)));
}

QTEST_MAIN(TestIconResources)
#include "test_icon_resources.moc"
