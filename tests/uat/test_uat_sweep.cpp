// UAT harness — Layer-1 config-matrix robustness sweep.
//
// The plan's Layer 1: instead of trusting the UI looks right, drive the
// MainWindow across a small matrix of display "knobs" a human can't
// exhaustively walk — application font size x layout direction (LTR /
// RTL) — and assert a layout invariant on the live widget tree: no
// visible interactive control collapses to zero size or is squeezed
// below the size it needs to render its content. That squeeze is the
// machine-checkable proxy for "text clipped / control overlapped" — the
// paper-cut that otherwise only surfaces in someone's hands at 200%
// system font or in an RTL locale.
//
// Deterministic, hard oracle, fast — so it lives in the `uat` suite
// beside the regression guards (it is NOT the speculative persona /
// vision tier, which is advisory and lives elsewhere).
//
// Knobs covered today (in-process, offscreen-safe): font scale and
// layout direction. Dark theme needs Settings::theme() wired to
// QStyleHints::setColorScheme (deferred); per-monitor DPI needs a
// per-process QT_SCALE_FACTOR matrix rather than an in-process toggle.

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/MainWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QLineEdit>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStringList>
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

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("Sweep fixture"));
    p.end();
    return path;
}

bool isInteractive(QWidget *w) {
    return qobject_cast<QAbstractButton *>(w) || qobject_cast<QLineEdit *>(w) ||
           qobject_cast<QComboBox *>(w);
}

} // namespace

class TestUatSweep : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void init();
    void cleanup();

    void layoutSurvivesFontAndDirection_data();
    void layoutSurvivesFontAndDirection();

  private:
    QTemporaryDir m_scratch;
    QFont m_baselineFont;
};

void TestUatSweep::initTestCase() {
    // Capture the untouched default before any row mutates it.
    m_baselineFont = QApplication::font();
}

void TestUatSweep::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatSweep::cleanup() {
    // Never leak swept global state into the next row or another binary.
    QApplication::setFont(m_baselineFont);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatSweep::layoutSurvivesFontAndDirection_data() {
    QTest::addColumn<int>("fontPt");    // 0 => baseline size
    QTest::addColumn<int>("direction"); // Qt::LayoutDirection

    QTest::newRow("baseline") << 0 << int(Qt::LeftToRight);
    QTest::newRow("large-font") << 22 << int(Qt::LeftToRight);
    QTest::newRow("rtl") << 0 << int(Qt::RightToLeft);
    QTest::newRow("rtl-large-font") << 22 << int(Qt::RightToLeft);
}

void TestUatSweep::layoutSurvivesFontAndDirection() {
    QFETCH(int, fontPt);
    QFETCH(int, direction);
    QVERIFY(m_scratch.isValid());

    // Fully specify global display state for this row so the result
    // never depends on what a previous row left behind.
    QFont f = m_baselineFont;
    if (fontPt > 0)
        f.setPointSize(fontPt);
    QApplication::setFont(f);
    QApplication::setLayoutDirection(Qt::LayoutDirection(direction));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString tag = QString::fromLatin1(QTest::currentDataTag());
    const QString pdf = writeTinyPdf(m_scratch.filePath(QStringLiteral("sweep_%1.pdf").arg(tag)));
    app->openFiles({pdf});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY2(mw, "MainWindow must realize under every font/direction cell");
    mw->resize(1100, 750);
    mw->show();
    QApplication::processEvents();

    int checked = 0;
    QStringList violations;
    const auto widgets = mw->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (!w->isVisible() || !isInteractive(w))
            continue;
        // Skip Qt-internal sub-widgets of compound controls (e.g. a
        // QSpinBox's "qt_spinbox_lineedit"): the parent sizes them
        // intentionally smaller than their own minimumSizeHint, so they
        // are not meaningful standalone layout targets.
        if (w->objectName().startsWith(QLatin1String("qt_")))
            continue;
        ++checked;
        const QSize sz = w->size();
        const QSize msh = w->minimumSizeHint();
        const QString id =
            QStringLiteral("%1{%2}").arg(QString::fromLatin1(w->metaObject()->className()),
                                         w->objectName().isEmpty() ? QStringLiteral("?")
                                                                   : w->objectName());
        if (sz.width() <= 0 || sz.height() <= 0) {
            violations << QStringLiteral("%1 collapsed to %2x%3").arg(id).arg(sz.width()).arg(
                sz.height());
        } else if (msh.isValid() && msh.width() > 0 && sz.width() < msh.width()) {
            violations << QStringLiteral("%1 width %2 < min %3").arg(id).arg(sz.width()).arg(
                msh.width());
        } else if (msh.isValid() && msh.height() > 0 && sz.height() < msh.height()) {
            violations << QStringLiteral("%1 height %2 < min %3").arg(id).arg(sz.height()).arg(
                msh.height());
        }
    }

    // A cell that realizes no visible interactive chrome would make the
    // assertion vacuous — flag that rather than pass silently.
    QVERIFY2(checked > 0, "Sweep found no visible interactive controls — chrome not realized?");
    QVERIFY2(violations.isEmpty(),
             qPrintable(QStringLiteral("Layout violations in cell [%1] (%2 controls checked):\n  %3")
                            .arg(tag)
                            .arg(checked)
                            .arg(violations.join(QStringLiteral("\n  ")))));
}

// Custom main: set a sandbox HOME before constructing Application so
// Settings / RecentFiles don't touch the real config dir.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatSweep tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_sweep.moc"
