// UAT harness — Layer-1 config-matrix robustness sweep.
//
// The plan's Layer 1: instead of trusting the UI looks right, drive the
// app's surfaces across a small matrix of display "knobs" a human can't
// exhaustively walk — application font size x layout direction (LTR /
// RTL) — and assert a layout invariant on the live widget tree: no
// visible interactive control collapses to zero size or is squeezed
// below the size it needs to render its content. That squeeze is the
// machine-checkable proxy for "text clipped / control overlapped" — the
// paper-cut that otherwise only surfaces in someone's hands at 200%
// system font or in an RTL locale.
//
// Surfaces swept:
//   - The MainWindow shell with a document open, including the Inspector
//     and Sidebar docks (shown explicitly; they are hidden by default).
//   - MyCardDialog — the densest plain-form dialog (~14 fields); the
//     HITL pass called it "huge with tons of options", so it is the most
//     likely to clip under a large font or RTL.
//
// Deterministic, hard oracle, fast — so it lives in the `uat` suite
// beside the regression guards (NOT the speculative persona / vision
// tier, which is advisory and lives elsewhere).
//
// Knobs covered today: font scale and layout direction (in-process,
// offscreen-safe), plus devicePixelRatio ∈ {1, 1.5, 2} injected
// per-process via QT_SCALE_FACTOR by the CMake dpr matrix
// (tests/uat/CMakeLists.txt, trailer_register_uat_dpr_matrix) — the whole
// sweep re-runs under each dpr, closing the HiDPI blind spot in
// docs/backlog/2026-07-16-hidpi-uat-harness.md. Dark theme still needs
// Settings::theme() wired to QStyleHints::setColorScheme (deferred).

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/MainWindow.h"
#include "ui/MyCardDialog.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFont>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPageSize>
#include <QScreen>
#include <QPainter>
#include <QPdfWriter>
#include <QStringList>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <QtTest/QtTest>

#include <cmath>

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

// devicePixelRatio requested for this process via QT_SCALE_FACTOR (the
// CMake dpr matrix, tests/uat/CMakeLists.txt); 1.0 when unset. See the
// twin helper in test_uat_thumbnail_sidebar.cpp for the rationale.
double requestedDpr() {
    bool ok = false;
    const double v = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&ok);
    return (ok && v > 0.0) ? v : 1.0;
}

double screenDpr() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->devicePixelRatio() : 1.0;
}

bool isInteractive(QWidget *w) {
    return qobject_cast<QAbstractButton *>(w) || qobject_cast<QLineEdit *>(w) ||
           qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w);
}

// Walk root's descendants and flag any visible interactive control that
// is collapsed or rendered below its minimumSizeHint. Skips Qt-internal
// sub-widgets (qt_*) whose compound parent sizes them intentionally
// small (e.g. a QSpinBox's "qt_spinbox_lineedit"). `checked` accumulates
// how many real controls were inspected so a vacuous pass is detectable.
QStringList collectLayoutViolations(QWidget *root, int &checked) {
    QStringList violations;
    const auto widgets = root->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (!w->isVisible() || !isInteractive(w))
            continue;
        if (w->objectName().startsWith(QLatin1String("qt_")))
            continue;
        // Qt's built-in QTabBar overflow scroller arrows are chrome Qt
        // sizes itself via a fixed style pixel-metric that ignores font
        // scaling; they are not app controls, so exempt them from the
        // size>=minimumSizeHint invariant (same rationale as the qt_* skip).
        if (w->objectName() == QLatin1String("ScrollLeftButton") ||
            w->objectName() == QLatin1String("ScrollRightButton"))
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
    return violations;
}

// The accessible name a screen reader announces for a button: its own
// accessibleName, else its action's accessibleName / text, else its
// visible text. Empty => the control reads as a bare "button".
QString effectiveButtonName(QAbstractButton *b) {
    if (!b->accessibleName().trimmed().isEmpty())
        return b->accessibleName().trimmed();
    if (auto *tb = qobject_cast<QToolButton *>(b)) {
        if (QAction *a = tb->defaultAction()) {
            // QAction has no accessibleName of its own; Qt derives a
            // button's accessible name from the action's text.
            if (!a->text().trimmed().isEmpty())
                return a->text().trimmed();
        }
    }
    return b->text().trimmed();
}

// Visible buttons that would read as a bare "button" — nothing a screen
// reader can announce. Skips Qt-internal buttons (qt_*, e.g. the toolbar
// overflow button) whose naming is Qt's responsibility, not ours.
QStringList collectUnnamedButtons(QWidget *root) {
    QStringList unnamed;
    for (QWidget *w : root->findChildren<QWidget *>()) {
        auto *b = qobject_cast<QAbstractButton *>(w);
        if (!b || !b->isVisible())
            continue;
        if (b->objectName().startsWith(QLatin1String("qt_")))
            continue;
        if (effectiveButtonName(b).isEmpty())
            unnamed << QStringLiteral("%1{%2}").arg(
                QString::fromLatin1(b->metaObject()->className()),
                b->objectName().isEmpty() ? QStringLiteral("?") : b->objectName());
    }
    return unnamed;
}

void addFontDirectionRows() {
    QTest::addColumn<int>("fontPt");    // 0 => baseline size
    QTest::addColumn<int>("direction"); // Qt::LayoutDirection

    QTest::newRow("baseline") << 0 << int(Qt::LeftToRight);
    QTest::newRow("large-font") << 22 << int(Qt::LeftToRight);
    QTest::newRow("rtl") << 0 << int(Qt::RightToLeft);
    QTest::newRow("rtl-large-font") << 22 << int(Qt::RightToLeft);
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
    void cardDialogSurvivesFontAndDirection_data();
    void cardDialogSurvivesFontAndDirection();
    void interactiveControlsHaveAccessibleNames();

  private:
    void applyCell(int fontPt, int direction);

    QTemporaryDir m_scratch;
    QFont m_baselineFont;
};

void TestUatSweep::initTestCase() {
    // Capture the untouched default before any row mutates it.
    m_baselineFont = QApplication::font();

    // Prove the dpr injection took. The CMake matrix runs this shell/toolbar
    // sweep under QT_SCALE_FACTOR ∈ {1, 1.5, 2}; the layout-collapse oracle
    // below is only a HiDPI test if the screen truly reports that dpr.
    // Fail loudly rather than let a {1.5, 2} run silently degrade to dpr = 1.
    const double wantDpr = requestedDpr();
    const double gotDpr = screenDpr();
    qInfo().noquote() << "DPR requested" << wantDpr << "primaryScreen" << gotDpr;
    QVERIFY2(std::abs(gotDpr - wantDpr) < 0.01,
             qPrintable(QStringLiteral("dpr injection did not take: "
                                       "QT_SCALE_FACTOR requested %1 but the "
                                       "primary screen reports dpr %2")
                            .arg(wantDpr)
                            .arg(gotDpr)));
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

void TestUatSweep::applyCell(int fontPt, int direction) {
    // Fully specify global display state for this row so the result
    // never depends on what a previous row left behind.
    QFont f = m_baselineFont;
    if (fontPt > 0)
        f.setPointSize(fontPt);
    QApplication::setFont(f);
    QApplication::setLayoutDirection(Qt::LayoutDirection(direction));
}

void TestUatSweep::layoutSurvivesFontAndDirection_data() { addFontDirectionRows(); }

void TestUatSweep::layoutSurvivesFontAndDirection() {
    QFETCH(int, fontPt);
    QFETCH(int, direction);
    QVERIFY(m_scratch.isValid());
    applyCell(fontPt, direction);

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
    // Realize every dock panel (all hidden by default) so their controls
    // are part of the sweep. Show all QDockWidgets rather than matching on
    // translatable window titles, which would break across locales/renames.
    for (auto *dock : mw->findChildren<QDockWidget *>())
        dock->show();
    QApplication::processEvents();

    int checked = 0;
    const QStringList violations = collectLayoutViolations(mw, checked);
    QVERIFY2(checked > 0, "Sweep found no visible interactive controls — chrome not realized?");
    QVERIFY2(violations.isEmpty(),
             qPrintable(
                 QStringLiteral("MainWindow layout violations in cell [%1] (%2 controls checked):\n  %3")
                     .arg(tag)
                     .arg(checked)
                     .arg(violations.join(QStringLiteral("\n  ")))));
}

void TestUatSweep::cardDialogSurvivesFontAndDirection_data() { addFontDirectionRows(); }

void TestUatSweep::cardDialogSurvivesFontAndDirection() {
    QFETCH(int, fontPt);
    QFETCH(int, direction);
    applyCell(fontPt, direction);

    // ~14 plain QLineEdits + an OK/Cancel button box. Shown at its own
    // size hint so a violation means the dialog's layout genuinely fails
    // to give a child its minimum under this font/direction.
    MyCardDialog dlg;
    dlg.resize(dlg.sizeHint());
    dlg.show();
    QApplication::processEvents();

    int checked = 0;
    const QStringList violations = collectLayoutViolations(&dlg, checked);
    const QString tag = QString::fromLatin1(QTest::currentDataTag());
    QVERIFY2(checked > 0, "MyCardDialog realized no interactive controls?");
    QVERIFY2(violations.isEmpty(),
             qPrintable(
                 QStringLiteral("MyCardDialog layout violations in cell [%1] (%2 controls checked):\n  %3")
                     .arg(tag)
                     .arg(checked)
                     .arg(violations.join(QStringLiteral("\n  ")))));
    dlg.close();
}

// A11y guard (audit A-CRIT-1): every visible interactive button must
// have a name a screen reader can announce, or it reads as a bare
// "button". Walks the realized MainWindow chrome (incl. the markup
// toolbar and the side docks). Font/direction-independent, so it runs
// once rather than across the matrix.
void TestUatSweep::interactiveControlsHaveAccessibleNames() {
    QVERIFY(m_scratch.isValid());
    const QString pdf = writeTinyPdf(m_scratch.filePath(QStringLiteral("a11y.pdf")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY2(mw, "MainWindow must realize");
    mw->resize(1100, 750);
    mw->show();
    for (auto *dock : mw->findChildren<QDockWidget *>())
        dock->show();
    QApplication::processEvents();

    const QStringList unnamed = collectUnnamedButtons(mw);
    QVERIFY2(unnamed.isEmpty(),
             qPrintable(QStringLiteral(
                            "Buttons a screen reader would read as just \"button\" (no accessible "
                            "name):\n  %1")
                            .arg(unnamed.join(QStringLiteral("\n  ")))));
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

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatSweep tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_sweep.moc"
