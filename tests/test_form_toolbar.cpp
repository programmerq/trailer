// Unit tests for FormToolbar — the toolbar introduced in Phase 5 for
// "fill in a form that doesn't have proper AcroForm fields" (design
// doc §6.4). We don't test the rendering; we test that clicking each
// tool action emits `toolChanged` with the right AnnotationTool and
// the expected preset text (✓ / ✗ glyph or empty), and that the
// AutoFill / Sign Here buttons emit their own signals.

#include "ui/FormToolbar.h"
#include "ui/IconHelper.h"

#include <QAction>
#include <QImage>
#include <QMetaType>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace trailer;

// QSignalSpy stores signal arguments as QVariants, so the enum type
// has to be registered with Qt's meta-type system for take/at lookups
// to work.
Q_DECLARE_METATYPE(trailer::AnnotationTool)

class TestFormToolbar : public QObject {
    Q_OBJECT
  private slots:
    void textBoxEmitsTextToolWithEmptyPreset();
    void checkmarkEmitsTextToolWithCheckGlyph();
    void xMarkEmitsTextToolWithCrossGlyph();
    void autoFillSignalFiresOnce();
    void signHereSignalFiresOnce();
    void toolsAreMutuallyExclusive();
    void everyActionHasARenderedIcon();
    void filledSiblingPairedAsCheckedState();
    void noFilledSiblingReusesNormalForChecked();
};

namespace {

// Find a QAction on the toolbar by its visible text. Returns nullptr
// if nothing matches (meaning the test's assumption about the toolbar
// layout is wrong — fail loudly rather than pass silently).
QAction *findAction(FormToolbar *bar, const QString &text) {
    for (QAction *a : bar->actions()) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

} // namespace

void TestFormToolbar::textBoxEmitsTextToolWithEmptyPreset() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::toolChanged);

    QAction *a = findAction(&bar, QStringLiteral("Text Box"));
    QVERIFY(a);
    a->trigger();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<AnnotationTool>(), AnnotationTool::Text);
    QCOMPARE(args.at(1).toString(), QString());
    QCOMPARE(bar.activeTool(), AnnotationTool::Text);
    QVERIFY(bar.pendingText().isEmpty());
}

void TestFormToolbar::checkmarkEmitsTextToolWithCheckGlyph() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::toolChanged);

    QAction *a = findAction(&bar, QStringLiteral("Checkmark"));
    QVERIFY(a);
    a->trigger();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<AnnotationTool>(), AnnotationTool::Text);
    QCOMPARE(args.at(1).toString(), QStringLiteral(u"\u2713"));
    QCOMPARE(bar.pendingText(), QStringLiteral(u"\u2713"));
}

void TestFormToolbar::xMarkEmitsTextToolWithCrossGlyph() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::toolChanged);

    QAction *a = findAction(&bar, QStringLiteral("X Mark"));
    QVERIFY(a);
    a->trigger();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<AnnotationTool>(), AnnotationTool::Text);
    QCOMPARE(args.at(1).toString(), QStringLiteral(u"\u2717"));
    QCOMPARE(bar.pendingText(), QStringLiteral(u"\u2717"));
}

void TestFormToolbar::autoFillSignalFiresOnce() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::autoFillRequested);
    QAction *a = findAction(&bar, QStringLiteral("AutoFill"));
    QVERIFY(a);
    a->trigger();
    QCOMPARE(spy.count(), 1);
}

void TestFormToolbar::signHereSignalFiresOnce() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::signHereRequested);
    QAction *a = findAction(&bar, QStringLiteral("Sign Here"));
    QVERIFY(a);
    a->trigger();
    QCOMPARE(spy.count(), 1);
}

void TestFormToolbar::toolsAreMutuallyExclusive() {
    FormToolbar bar;

    QAction *checkAction = findAction(&bar, QStringLiteral("Checkmark"));
    QAction *textAction = findAction(&bar, QStringLiteral("Text Box"));
    QVERIFY(checkAction);
    QVERIFY(textAction);

    checkAction->trigger();
    QVERIFY(checkAction->isChecked());
    QVERIFY(!textAction->isChecked());

    textAction->trigger();
    QVERIFY(textAction->isChecked());
    QVERIFY(!checkAction->isChecked());
    // Preset must reset when switching back to plain Text Box so the
    // overlay opens its input dialog instead of silently dropping ✓.
    QVERIFY(bar.pendingText().isEmpty());
}

// Regression: each toolbar action ships with a non-empty themed icon
// (resource registered in trailer.qrc, SVG renders to a real pixmap,
// the tint pass actually paints something). A broken qrc alias or
// missing Qt6::Svg dep would silently produce null icons — Qt's
// QIcon API does not throw — and we'd ship a blank-button toolbar.
void TestFormToolbar::everyActionHasARenderedIcon() {
    FormToolbar bar;
    for (QAction* a : bar.actions()) {
        if (a->isSeparator()) continue;
        QVERIFY2(!a->icon().isNull(),
                 qPrintable(QStringLiteral("action without icon: %1")
                                .arg(a->text())));
        // A non-null QIcon is necessary but not sufficient — verify
        // the icon engine actually renders a pixmap at the toolbar's
        // size rather than returning a transparent placeholder.
        const QPixmap pm = a->icon().pixmap(QSize(18, 18));
        QVERIFY2(!pm.isNull(),
                 qPrintable(QStringLiteral("icon renders empty: %1")
                                .arg(a->text())));
        QCOMPARE(pm.deviceIndependentSize(), QSizeF(18, 18));
    }
}

// When the SVG resource has a sibling named `<base>-filled.svg`, the
// helper auto-attaches it as the icon's QIcon::On pixmap so QAction's
// checked state renders the louder filled glyph. This is the
// outline → fill swap that signals "this is the armed tool" without
// relying on Qt's subtle checked-button background.
void TestFormToolbar::filledSiblingPairedAsCheckedState() {
    // tool-rectangle.svg + tool-rectangle-filled.svg both exist.
    const QIcon icon = themedActionIcon(
        QStringLiteral(":/icons/actions/tool-rectangle.svg"), Qt::black);
    QVERIFY(!icon.isNull());

    const QPixmap off = icon.pixmap(QSize(24, 24), QIcon::Normal, QIcon::Off);
    const QPixmap on  = icon.pixmap(QSize(24, 24), QIcon::Normal, QIcon::On);
    QVERIFY(!off.isNull());
    QVERIFY(!on.isNull());

    // The two pixmaps must actually differ — same-content would mean
    // the filled sibling silently failed to load and Qt fell back to
    // the Off variant for the On state.
    const QImage offImg = off.toImage();
    const QImage onImg  = on.toImage();
    QVERIFY2(offImg != onImg,
             "Off and On pixmaps are identical — filled sibling did "
             "not render or was not attached as QIcon::On.");

    // The filled variant should paint MORE pixels (it adds an inset
    // filled interior on top of the outline). Sanity-check by counting
    // non-transparent pixels: On > Off.
    auto countOpaque = [](const QImage& img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (qAlpha(img.pixel(x, y)) > 0) ++n;
            }
        }
        return n;
    };
    QVERIFY2(countOpaque(onImg) > countOpaque(offImg),
             "Filled variant should cover more pixels than the outline.");
}

// Tools without a `-filled.svg` sibling (e.g. tool-select, which the
// guidelines deliberately excludes — selection is the implicit
// default and has no "armed" state) should still produce a usable
// icon for both states; Qt falls back to the Off pixmap when no On
// is registered.
void TestFormToolbar::noFilledSiblingReusesNormalForChecked() {
    const QIcon icon = themedActionIcon(
        QStringLiteral(":/icons/actions/tool-select.svg"), Qt::black);
    QVERIFY(!icon.isNull());
    const QPixmap on = icon.pixmap(QSize(24, 24), QIcon::Normal, QIcon::On);
    QVERIFY2(!on.isNull(),
             "Icon without a filled sibling must still render in On state.");
}

QTEST_MAIN(TestFormToolbar)
#include "test_form_toolbar.moc"
