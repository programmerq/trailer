// Known bugs in the document window, driven through the real Application and
// MainWindow (offscreen): menu actions, key events, and mouse events placed
// from rendered pixels. Each check states the correct behaviour; checks that
// fail today carry QEXPECT_FAIL with their tracker id.
//
// Three ctest entries run this binary: the default offscreen logical DPI (96,
// as on Windows and Linux), QT_FONT_DPI=72 (as on macOS), and
// QT_SCALE_FACTOR=2 (dpr 2).

#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "document/IDocument.h"
#include "document/PdfEditor.h"
#include "settings/DocumentTypeDefaults.h"
#include "ui/AnnotationOverlay.h"
#include "ui/DocumentView.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/SearchBar.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfView>
#include <QPdfWriter>
#include <QProgressDialog>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QToolBar>
#include <QtTest/QtTest>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <vector>

using namespace trailer;

namespace {

const QString kDirtyMarker = QString::fromUtf8("\xE2\x80\xA2"); // "•" title prefix

Application *theApp() {
    return qobject_cast<Application *>(qApp);
}

bool logicalDpiIs72() {
    return qFuzzyCompare(QGuiApplication::primaryScreen()->logicalDotsPerInch(), 72.0);
}

QAction *actionNamed(QWidget *w, const QString &objectName) {
    for (QAction *a : w->findChildren<QAction *>())
        if (a->objectName() == objectName)
            return a;
    return nullptr;
}

QAction *actionTexted(QWidget *w, const QString &text) {
    for (QAction *a : w->findChildren<QAction *>())
        if (a->text().remove(QLatin1Char('&')) == text)
            return a;
    return nullptr;
}

QAction *actionWithShortcut(QWidget *w, QKeySequence::StandardKey key) {
    for (QAction *a : w->findChildren<QAction *>())
        if (a->shortcut() == QKeySequence(key))
            return a;
    return nullptr;
}

QAction *markupTool(MainWindow *mw, const QString &name) {
    for (QToolBar *tb : mw->findChildren<QToolBar *>(QStringLiteral("MarkupToolbar")))
        for (QAction *a : tb->actions())
            if (a->text().remove(QLatin1Char('&')) == name)
                return a;
    return nullptr;
}

DocumentView *documentView(MainWindow *mw) {
    return mw->findChild<DocumentView *>();
}

IDocument *currentDocument(MainWindow *mw) {
    DocumentView *dv = documentView(mw);
    return dv ? dv->currentDocument() : nullptr;
}

QPdfView *pdfView(MainWindow *mw) {
    DocumentView *dv = documentView(mw);
    return dv && dv->currentWidget() ? dv->currentWidget()->findChild<QPdfView *>() : nullptr;
}

QPoint documentViewPos(MainWindow *mw) {
    return documentView(mw)->mapTo(mw, QPoint(0, 0));
}

MainWindow *windowShowing(const QString &path) {
    const QString want = QFileInfo(path).canonicalFilePath();
    for (MainWindow *w : theApp()->windows()) {
        for (int i = 0; w && i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d &&
                QFileInfo(d->filePath()).canonicalFilePath() == want)
                return w;
        }
    }
    return nullptr;
}

// Opens the files the way the OS hands them to the app, gives the window a
// fixed size, and makes it the active window.
MainWindow *openWindow(const QStringList &paths) {
    theApp()->openFiles(paths);
    MainWindow *mw = windowShowing(paths.first());
    if (!mw)
        return nullptr;
    mw->resize(1280, 800);
    if (!QTest::qWaitForWindowExposed(mw))
        return nullptr;
    mw->activateWindow();
    if (!QTest::qWaitForWindowActive(mw))
        return nullptr;
    return mw;
}

// Delivers posted events (layout requests, zero-delay timers, deferred deletes).
void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

bool saveInFlight(MainWindow *mw) {
    return !mw->findChildren<QProgressDialog *>().isEmpty();
}

// Left-button drag over `w`, delivered through its window like a real pointer.
void dragOn(QWidget *w, QPoint from, QPoint to) {
    QWidget *top = w->window();
    QWindow *win = top->windowHandle();
    const QPoint a = w->mapTo(top, from);
    const QPoint b = w->mapTo(top, to);
    QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseMove(win, a + (b - a) / 2);
    QTest::mouseMove(win, b);
    QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, b);
}

void clickOn(QWidget *w, QPoint at) {
    QWidget *top = w->window();
    QTest::mouseClick(top->windowHandle(), Qt::LeftButton, Qt::NoModifier, w->mapTo(top, at));
}

bool isBlack(QRgb p) {
    return qRed(p) < 64 && qGreen(p) < 64 && qBlue(p) < 64;
}

QImage rendering(QWidget *w) {
    return w->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

QRect toLogical(const QRect &r, qreal dpr) {
    return QRect(QPoint(int(r.left() / dpr), int(r.top() / dpr)),
                 QPoint(int(r.right() / dpr), int(r.bottom() / dpr)));
}

// Bounding box of the near-black pixels, in widget coordinates.
QRect inkBox(const QImage &img) {
    int x0 = INT_MAX, y0 = INT_MAX, x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (isBlack(row[x])) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < 0)
        return {};
    return toLogical(QRect(QPoint(x0, y0), QPoint(x1, y1)), img.devicePixelRatio());
}

// Bounding boxes of the connected near-black areas, in widget coordinates.
QList<QRect> blackBoxes(const QImage &img) {
    const int w = img.width();
    const int h = img.height();
    std::vector<char> seen(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    auto seenAt = [&](int x, int y) -> char & {
        return seen[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    };
    auto blackAt = [&](int x, int y) {
        return isBlack(reinterpret_cast<const QRgb *>(img.constScanLine(y))[x]);
    };
    QList<QRect> boxes;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (seenAt(x, y) || !blackAt(x, y))
                continue;
            QRect box(x, y, 1, 1);
            QList<QPoint> todo{QPoint(x, y)};
            seenAt(x, y) = 1;
            while (!todo.isEmpty()) {
                const QPoint p = todo.takeLast();
                box |= QRect(p, QSize(1, 1));
                for (const QPoint &n :
                     {p + QPoint(1, 0), p - QPoint(1, 0), p + QPoint(0, 1), p - QPoint(0, 1)}) {
                    if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h ||
                        seenAt(n.x(), n.y()) || !blackAt(n.x(), n.y()))
                        continue;
                    seenAt(n.x(), n.y()) = 1;
                    todo.append(n);
                }
            }
            boxes.append(toLogical(box, img.devicePixelRatio()));
        }
    }
    return boxes;
}

// Waits until four consecutive renderings agree on the ink box. Pages are
// rendered off the GUI thread, so a single grab can show a stale scale.
bool settledInkBox(QWidget *w, QRect *out) {
    QRect last;
    int same = 0;
    const bool ok = QTest::qWaitFor([&] {
        const QRect now = inkBox(rendering(w));
        same = (now.isValid() && now == last) ? same + 1 : 0;
        last = now;
        return same >= 3;
    });
    *out = last;
    return ok;
}

// Page fill of the multi-page fixture; pages not rendered yet paint white.
const QColor kPageFill(255, 236, 170);

bool isPageFill(QRgb p) {
    return std::abs(qRed(p) - kPageFill.red()) < 12 &&
           std::abs(qGreen(p) - kPageFill.green()) < 12 &&
           std::abs(qBlue(p) - kPageFill.blue()) < 12;
}

// Waits until the page under the viewport centre is rendered, then returns
// the marker nearest the centre once four renderings agree on it.
bool settledCentreMarker(QWidget *vp, QRect *out) {
    QRect last;
    int same = 0;
    const bool ok = QTest::qWaitFor([&] {
        const QImage img = rendering(vp);
        const qreal dpr = img.devicePixelRatio();
        const QPoint c = vp->rect().center();
        const QRgb under = img.pixel(int(c.x() * dpr), int(c.y() * dpr));
        QRect now;
        if (isPageFill(under) || isBlack(under)) {
            int best = INT_MAX;
            for (const QRect &r : blackBoxes(img)) {
                const int d = (r.center() - c).manhattanLength();
                if (d < best) {
                    best = d;
                    now = r;
                }
            }
        }
        same = (now.isValid() && now == last) ? same + 1 : 0;
        last = now;
        return same >= 3;
    });
    *out = last;
    return ok;
}

// The two-image fixture: alpha.png is red, bravo.png is blue.
QString imageShown(QWidget *w) {
    const QImage img = rendering(w);
    const QColor c = img.pixelColor(img.rect().center());
    if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
        return QStringLiteral("alpha.png");
    if (c.blue() > 200 && c.red() < 60 && c.green() < 60)
        return QStringLiteral("bravo.png");
    return {};
}

QString rectText(const QRectF &r) {
    return QStringLiteral("(%1,%2 %3x%4)")
        .arg(r.x(), 0, 'f', 1)
        .arg(r.y(), 0, 'f', 1)
        .arg(r.width(), 0, 'f', 1)
        .arg(r.height(), 0, 'f', 1);
}

QByteArray fileHash(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
}

// One A4 page: "INVOICE" in black 36 pt near the top, body lines in grey.
QString writeInvoicePdf(const QString &path) {
    QPdfWriter w(path);
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setResolution(72);
    w.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p(&w);
    QFont heading(QStringLiteral("DejaVu Sans"));
    heading.setPointSize(36);
    p.setFont(heading);
    p.setPen(Qt::black);
    p.drawText(QRectF(60, 100, 475, 60), Qt::AlignLeft, QStringLiteral("INVOICE"));
    QFont body(QStringLiteral("DejaVu Sans"));
    body.setPointSize(11);
    p.setFont(body);
    p.setPen(QColor(140, 140, 140));
    for (int line = 0; line < 20; ++line)
        p.drawText(QRectF(60, 220 + line * 18, 475, 18), Qt::AlignLeft,
                   QStringLiteral("Line item %1 .......... 10.00").arg(line + 1));
    p.end();
    return path;
}

// A4 pages filled with kPageFill, each with eight black markers at a
// horizontal offset unique to (page, row), so a marker identifies the
// document point it sits on.
QString writeMarkedPdf(const QString &path, int pages) {
    QPdfWriter w(path);
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setResolution(72);
    w.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p(&w);
    for (int page = 0; page < pages; ++page) {
        if (page > 0)
            w.newPage();
        p.fillRect(QRectF(0, 0, 595, 842), kPageFill);
        for (int row = 0; row < 8; ++row)
            p.fillRect(QRectF(40 + 14 * (page * 8 + row), 60 + 95 * row, 24, 16), Qt::black);
    }
    p.end();
    return path;
}

bool writeImage(const QString &path, const QColor &colour, const char *format = nullptr) {
    QImage img(400, 300, QImage::Format_RGB32);
    img.fill(colour);
    return img.save(path, format, 95);
}

// The "INVOICE" glyph bounds in page points, as QPdfDocument reports them.
QRectF invoiceBounds(const QString &path) {
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return {};
    const QPdfSelection sel = pdf.getSelectionAtIndex(0, 0, 7);
    return sel.text() == QLatin1String("INVOICE") ? sel.boundingRectangle() : QRectF();
}

// Leaves the default Fit Page zoom, or switches to the zoom the row names.
bool applyZoomRow(MainWindow *mw, const QString &row) {
    IDocument *doc = currentDocument(mw);
    if (!doc || !QTest::qWaitFor([&] { return doc->zoomMode() == ZoomMode::FitInView; }))
        return false;
    if (row == QLatin1String("fitPage"))
        return true;
    const bool fitWidth = row == QLatin1String("fitWidth");
    QAction *a = fitWidth ? actionTexted(mw, QStringLiteral("Fit to Width"))
                          : actionNamed(mw, QStringLiteral("action.view.actualSize"));
    if (!a)
        return false;
    a->trigger();
    const ZoomMode want = fitWidth ? ZoomMode::FitToWidth : ZoomMode::Actual;
    return QTest::qWaitFor([&] { return doc->zoomMode() == want; });
}

// A main-toolbar control: "<position in the toolbar> <name>", and its
// geometry in window coordinates (null while hidden).
struct ToolbarControl {
    QString name;
    QRect geometry;
};

QList<ToolbarControl> mainToolbarControls(MainWindow *mw) {
    QList<ToolbarControl> out;
    auto *tb = mw->findChild<QToolBar *>(QStringLiteral("MainToolbar"));
    const QList<QAction *> actions = tb->actions();
    for (qsizetype i = 0; i < actions.size(); ++i) {
        QAction *a = actions.at(i);
        QWidget *w = tb->widgetForAction(a);
        // The stretchable spacer is a plain QWidget, not a control.
        if (!w || w->metaObject() == &QWidget::staticMetaObject)
            continue;
        QString name =
            a->isSeparator() ? QStringLiteral("separator") : a->text().remove(QLatin1Char('&'));
        for (const QString &fallback :
             {w->accessibleName(), w->toolTip(), QString::fromLatin1(w->metaObject()->className())})
            if (name.isEmpty())
                name = fallback;
        out.append({QStringLiteral("%1 %2").arg(i).arg(name),
                    w->isVisible() ? QRect(w->mapTo(mw, QPoint(0, 0)), w->size()) : QRect()});
    }
    return out;
}

// "<name> at x,y wxh" for each control shown in `controls` that is also
// shown in `alsoShownIn`.
QStringList placements(const QList<ToolbarControl> &controls,
                       const QList<ToolbarControl> &alsoShownIn) {
    QStringList out;
    for (const ToolbarControl &c : controls) {
        const bool shownInBoth =
            !c.geometry.isNull() &&
            std::any_of(alsoShownIn.begin(), alsoShownIn.end(), [&](const ToolbarControl &o) {
                return o.name == c.name && !o.geometry.isNull();
            });
        if (shownInBoth)
            out << QStringLiteral("%1 at %2,%3 %4x%5")
                       .arg(c.name)
                       .arg(c.geometry.x())
                       .arg(c.geometry.y())
                       .arg(c.geometry.width())
                       .arg(c.geometry.height());
    }
    return out;
}

QString toolbarsAndDocumentY(MainWindow *mw) {
    QStringList parts;
    for (const char *name : {"MainToolbar", "MarkupToolbar", "FormToolbar"}) {
        auto *tb = mw->findChild<QToolBar *>(QString::fromLatin1(name));
        parts << QStringLiteral("%1 %2").arg(QString::fromLatin1(name),
                                             tb && tb->isVisible() ? QStringLiteral("shown")
                                                                   : QStringLiteral("hidden"));
    }
    parts << QStringLiteral("document view at y=%1").arg(documentViewPos(mw).y());
    return parts.join(QStringLiteral(", "));
}

} // namespace

class KnownBugsWindow : public QObject {
    Q_OBJECT

  private slots:
    void init();
    void cleanup();

    void dragAcrossWordCopiesThatWord_data();
    void dragAcrossWordCopiesThatWord();
    void rectangleAroundWordIsSavedAroundIt_data();
    void rectangleAroundWordIsSavedAroundIt();
    void currentDocumentFollowsDraggedTab();
    void inspectorShowsMarkSelectedOnEitherTab_data();
    void inspectorShowsMarkSelectedOnEitherTab();
    void saveLeavesUneditedFileUntouched_data();
    void saveLeavesUneditedFileUntouched();
    void undoToSavedStateClearsDirtyMarker_data();
    void undoToSavedStateClearsDirtyMarker();
    void editMarksWindowModified();
    void readingPositionSurvivesSaveAndUndo_data();
    void readingPositionSurvivesSaveAndUndo();
    void findAndEscapeLeaveLayoutUnchanged();
    void viewModeSwitchKeepsToolbarsAndDocumentInPlace();
    void pdfWindowShowsNoStatusBar();

  private:
    static void wordZoomRows();
    QString fixture(const QString &suffix) const;

    QTemporaryDir m_dir;
};

QString KnownBugsWindow::fixture(const QString &suffix) const {
    QString name = QString::fromLatin1(QTest::currentTestFunction());
    if (const char *tag = QTest::currentDataTag(); tag && *tag)
        name += QLatin1Char('-') + QString::fromLatin1(tag);
    return m_dir.filePath(name + suffix);
}

void KnownBugsWindow::init() {
    QVERIFY(m_dir.isValid());
    // Closing a window stores view state that the next open of that type
    // restores; start every test from none.
    for (DocumentType t : {DocumentType::Pdf, DocumentType::Image})
        theApp()->documentTypeDefaults().setForType(t, DocumentTypeDefault{});
    theApp()->documentTypeDefaults().save();
}

void KnownBugsWindow::cleanup() {
    for (MainWindow *w : theApp()->windows()) {
        if (!w)
            continue;
        w->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
        w->close();
    }
    QTRY_COMPARE(theApp()->windowCount(), 0);
}

void KnownBugsWindow::wordZoomRows() {
    QTest::addColumn<QString>("zoom");
    QTest::newRow("fitPage") << QStringLiteral("fitPage");
    QTest::newRow("fitWidth") << QStringLiteral("fitWidth");
    // Correct at 72 DPI; at 96 DPI the 96/72 screen scale is missing.
    QTest::newRow("actualSize") << QStringLiteral("actualSize");
}

void KnownBugsWindow::dragAcrossWordCopiesThatWord_data() {
    wordZoomRows();
}

// T-102: Marks and text selection land on different content than the user pointed at
void KnownBugsWindow::dragAcrossWordCopiesThatWord() {
    QFETCH(QString, zoom);
    const QString path = writeInvoicePdf(fixture(QStringLiteral(".pdf")));
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    QPdfView *view = pdfView(mw);
    QVERIFY(view);
    QVERIFY(applyZoomRow(mw, zoom));

    QRect word;
    QVERIFY(settledInkBox(view->viewport(), &word));
    QApplication::clipboard()->setText(QStringLiteral("(clipboard untouched)"));
    const int y = word.center().y();
    dragOn(view->viewport(), QPoint(word.left() + 2, y), QPoint(word.right() - 2, y));
    QTest::keySequence(mw->windowHandle(), QKeySequence::Copy);

    QEXPECT_FAIL("fitPage", "T-102: Fit Page maps the drag to the wrong text", Abort);
    QEXPECT_FAIL("fitWidth", "T-102: Fit Width maps the drag to the wrong text", Abort);
    if (!logicalDpiIs72())
        QEXPECT_FAIL("actualSize", "T-102: at 96 DPI the drag misses the screen scale", Abort);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("INVOICE"));
}

void KnownBugsWindow::rectangleAroundWordIsSavedAroundIt_data() {
    wordZoomRows();
}

// T-102: Marks and text selection land on different content than the user pointed at
void KnownBugsWindow::rectangleAroundWordIsSavedAroundIt() {
    QFETCH(QString, zoom);
    const QString path = writeInvoicePdf(fixture(QStringLiteral(".pdf")));
    const QRectF wordPt = invoiceBounds(path);
    QVERIFY(!wordPt.isEmpty());
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    QPdfView *view = pdfView(mw);
    IDocument *doc = currentDocument(mw);
    QVERIFY(view && doc);
    QVERIFY(applyZoomRow(mw, zoom));

    const int documentY = documentViewPos(mw).y();
    actionNamed(mw, QStringLiteral("action.view.markupToolbar"))->trigger();
    QTRY_VERIFY(documentViewPos(mw).y() > documentY);
    settle();
    QRect word;
    QVERIFY(settledInkBox(view->viewport(), &word));
    QAction *rectangle = markupTool(mw, QStringLiteral("Rectangle"));
    QVERIFY(rectangle);
    rectangle->trigger();
    dragOn(view->viewport(), word.topLeft() - QPoint(6, 6), word.bottomRight() + QPoint(6, 6));
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);
    QCOMPARE(store->count(), 1);

    actionNamed(mw, QStringLiteral("action.file.save"))->trigger();
    QTRY_VERIFY(!saveInFlight(mw) && !doc->isDirty());
    PdfEditor saved;
    QVERIFY(saved.load(path));
    const std::vector<Annotation> marks = saved.readAnnotations();
    QCOMPARE(int(marks.size()), 1);
    const QRectF box = marks.front().bounds;
    // The box must contain the word, with 2 pt of slack per side, and reach no
    // further out than the 6 px it was drawn outside the ink (plus 1 px for
    // the ink box's rounding), in points at the zoom shown, plus that slack.
    const double pxPerPt = word.width() / wordPt.width();
    const double slack = 2.0;
    const double reach = 7.0 / pxPerPt + slack;
    const bool encloses =
        box.left() <= wordPt.left() + slack && box.top() <= wordPt.top() + slack &&
        box.right() >= wordPt.right() - slack && box.bottom() >= wordPt.bottom() - slack;
    const bool hugs = box.left() >= wordPt.left() - reach && box.top() >= wordPt.top() - reach &&
                      box.right() <= wordPt.right() + reach &&
                      box.bottom() <= wordPt.bottom() + reach;

    QEXPECT_FAIL("fitPage", "T-102: Fit Page stores the mark at the wrong place", Abort);
    QEXPECT_FAIL("fitWidth", "T-102: Fit Width stores the mark at the wrong place", Abort);
    if (!logicalDpiIs72())
        QEXPECT_FAIL("actualSize", "T-102: at 96 DPI the mark misses the screen scale", Abort);
    QVERIFY2(encloses && hugs,
             qPrintable(QStringLiteral("saved box %1 pt; it must contain the word %2 pt and stay "
                                       "within %3 pt of it")
                            .arg(rectText(box), rectText(wordPt))
                            .arg(reach, 0, 'f', 1)));
}

// T-07: After a tab is dragged, commands act on the other document
void KnownBugsWindow::currentDocumentFollowsDraggedTab() {
    // Two images opened together share one tabbed window.
    const QString alpha = m_dir.filePath(QStringLiteral("alpha.png"));
    const QString bravo = m_dir.filePath(QStringLiteral("bravo.png"));
    QVERIFY(writeImage(alpha, Qt::red));
    QVERIFY(writeImage(bravo, Qt::blue));
    MainWindow *mw = openWindow({alpha, bravo});
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 2);
    DocumentView *dv = documentView(mw);
    QTabBar *bar = dv->tabBar();
    QTRY_VERIFY(bar->isVisible());
    QWidget *firstTab = dv->widget(0);

    // Drag the first tab past the middle of the second.
    QWindow *win = mw->windowHandle();
    const QPoint from = bar->mapTo(mw, bar->tabRect(0).center());
    const QPoint to =
        bar->mapTo(mw, bar->tabRect(1).center() + QPoint(bar->tabRect(1).width() / 4, 0));
    QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 8; ++step)
        QTest::mouseMove(win, from + (to - from) * step / 8);
    QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(dv->widget(1), firstTab);

    for (int i = 0; i < dv->count(); ++i) {
        clickOn(bar, bar->tabRect(i).center());
        QCOMPARE(dv->currentIndex(), i);
        QString shown;
        QTRY_VERIFY(!(shown = imageShown(dv->currentWidget())).isEmpty());
        QEXPECT_FAIL("", "T-07: the document list is not reordered with the tabs", Abort);
        QCOMPARE(QFileInfo(dv->currentDocument()->filePath()).fileName(), shown);
    }
}

void KnownBugsWindow::inspectorShowsMarkSelectedOnEitherTab_data() {
    QTest::addColumn<int>("tab");
    QTest::newRow("openedFirst") << 0;
    QTest::newRow("openedSecond") << 1;
}

// T-133: In a two-tab window, the Inspector, crop, Instant Alpha and Smart Lasso can ignore the
// shown tab
void KnownBugsWindow::inspectorShowsMarkSelectedOnEitherTab() {
    QFETCH(int, tab);
    // Two images opened together share one tabbed window, showing the one
    // opened second. The openedFirst row switches tabs once; neither row
    // switches after that.
    const QString alpha = m_dir.filePath(QStringLiteral("alpha.png"));
    const QString bravo = m_dir.filePath(QStringLiteral("bravo.png"));
    QVERIFY(writeImage(alpha, Qt::red));
    QVERIFY(writeImage(bravo, Qt::blue));
    MainWindow *mw = openWindow({alpha, bravo});
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 2);
    DocumentView *dv = documentView(mw);
    QTabBar *bar = dv->tabBar();
    QTRY_VERIFY(bar->isVisible());
    QCOMPARE(dv->currentIndex(), 1);
    if (tab != 1)
        clickOn(bar, bar->tabRect(tab).center());
    QCOMPARE(dv->currentIndex(), tab);

    // Show the Inspector on its Properties tab, and the markup toolbar. A
    // window restores the panes the last one closed with, so only turn on.
    QAction *showInspector = actionTexted(mw, QStringLiteral("Inspector"));
    QVERIFY(showInspector && showInspector->isCheckable());
    if (!showInspector->isChecked())
        showInspector->trigger();
    auto *pane = mw->findChild<Inspector *>(QStringLiteral("trailer.inspector"));
    QTRY_VERIFY(pane && pane->isVisible());
    auto *paneTabs = pane->findChild<QTabWidget *>();
    QVERIFY(paneTabs);
    int properties = -1;
    for (int i = 0; i < paneTabs->count(); ++i)
        if (paneTabs->tabText(i).remove(QLatin1Char('&')) == QStringLiteral("Properties"))
            properties = i;
    QVERIFY(properties >= 0);
    clickOn(paneTabs->tabBar(), paneTabs->tabBar()->tabRect(properties).center());
    QCOMPARE(paneTabs->currentIndex(), properties);
    QAction *showMarkup = actionNamed(mw, QStringLiteral("action.view.markupToolbar"));
    QVERIFY(showMarkup && showMarkup->isCheckable());
    if (!showMarkup->isChecked())
        showMarkup->trigger();
    settle();

    // Draw a rectangle on the shown image, then select it with a click.
    auto *layer = dv->currentWidget()->findChild<AnnotationOverlay *>();
    QVERIFY(layer && layer->isVisible());
    const QPoint centre = layer->rect().center();
    QAction *rectangle = markupTool(mw, QStringLiteral("Rectangle"));
    QAction *select = markupTool(mw, QStringLiteral("Select"));
    QVERIFY(rectangle && select);
    rectangle->trigger();
    dragOn(layer, centre - QPoint(60, 40), centre + QPoint(60, 40));
    QCOMPARE(currentDocument(mw)->annotations()->count(), 1);
    select->trigger();
    clickOn(layer, centre);
    settle();

    bool shown = false;
    for (QLabel *label : pane->findChildren<QLabel *>())
        shown = shown || (label->isVisible() && label->text() == QStringLiteral("Rectangle"));
    QEXPECT_FAIL("openedSecond", "T-133: this tab's marks aren't connected to the Inspector",
                 Abort);
    QVERIFY2(shown, "the Inspector's Properties tab doesn't show the selected rectangle");
}

void KnownBugsWindow::saveLeavesUneditedFileUntouched_data() {
    QTest::addColumn<QString>("suffix");
    QTest::newRow("pdf") << QStringLiteral(".pdf");
    QTest::newRow("jpeg") << QStringLiteral(".jpg");
}

// T-96: Save rewrites unedited files
void KnownBugsWindow::saveLeavesUneditedFileUntouched() {
    QFETCH(QString, suffix);
    const QString path = fixture(suffix);
    if (suffix == QLatin1String(".pdf"))
        writeMarkedPdf(path, 2);
    else
        QVERIFY(writeImage(path, QColor(30, 90, 160), "JPEG"));
    const QDateTime stamp(QDate(2020, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(stamp, QFileDevice::FileModificationTime));
    }
    const QByteArray hashBefore = fileHash(path);
    QVERIFY(!hashBefore.isEmpty());

    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    IDocument *doc = currentDocument(mw);
    QVERIFY(doc);
    QTRY_VERIFY(!doc->contentSizePending());
    QVERIFY(!doc->isDirty());
    actionNamed(mw, QStringLiteral("action.file.save"))->trigger();
    QTRY_VERIFY(!saveInFlight(mw));

    QEXPECT_FAIL("pdf", "T-96: Save rewrites an unedited PDF", Abort);
    QEXPECT_FAIL("jpeg", "T-96: Save re-encodes an unedited JPEG", Abort);
    QCOMPARE(fileHash(path), hashBefore);
    QCOMPARE(QFileInfo(path).lastModified(QTimeZone::UTC), stamp);
}

void KnownBugsWindow::undoToSavedStateClearsDirtyMarker_data() {
    QTest::addColumn<QString>("suffix");
    QTest::addColumn<bool>("mark");
    QTest::newRow("pdfRotate") << QStringLiteral(".pdf") << false;
    QTest::newRow("pdfMark") << QStringLiteral(".pdf") << true;
    QTest::newRow("imageRotate") << QStringLiteral(".png") << false;
    // Correct today: undoing an image mark clears the marker.
    QTest::newRow("imageMark") << QStringLiteral(".png") << true;
}

// T-104: Undo back to the saved state leaves the document dirty, and the window is never marked
// modified
void KnownBugsWindow::undoToSavedStateClearsDirtyMarker() {
    QFETCH(QString, suffix);
    QFETCH(bool, mark);
    const QString path = fixture(suffix);
    if (suffix == QLatin1String(".pdf"))
        writeMarkedPdf(path, 2);
    else
        QVERIFY(writeImage(path, QColor(30, 90, 160)));
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    IDocument *doc = currentDocument(mw);
    QVERIFY(doc);
    QTRY_VERIFY(!doc->contentSizePending());
    QVERIFY(!mw->windowTitle().startsWith(kDirtyMarker));

    if (mark) {
        actionNamed(mw, QStringLiteral("action.view.markupToolbar"))->trigger();
        QAction *rectangle = markupTool(mw, QStringLiteral("Rectangle"));
        QVERIFY(rectangle);
        rectangle->trigger();
        settle();
        QWidget *surface = documentView(mw)->currentWidget();
        if (QPdfView *view = pdfView(mw))
            surface = view->viewport();
        const QPoint c = surface->rect().center();
        dragOn(surface, c - QPoint(40, 30), c + QPoint(40, 30));
        AnnotationStore *store = doc->annotations();
        QVERIFY(store);
        QCOMPARE(store->count(), 1);
    } else {
        actionNamed(mw, QStringLiteral("action.tools.rotateRight"))->trigger();
    }
    QVERIFY(doc->isDirty());
    QVERIFY(mw->windowTitle().startsWith(kDirtyMarker));

    actionWithShortcut(mw, QKeySequence::Undo)->trigger();
    QVERIFY(!doc->canUndo());
    QEXPECT_FAIL("pdfRotate", "T-104: undoing a page rotation leaves the PDF dirty", Abort);
    QEXPECT_FAIL("pdfMark", "T-104: undoing a mark leaves the PDF dirty", Abort);
    QEXPECT_FAIL("imageRotate", "T-104: undoing a rotation leaves the image dirty", Abort);
    QVERIFY2(!mw->windowTitle().startsWith(kDirtyMarker), qPrintable(mw->windowTitle()));
    QVERIFY(!doc->isDirty());
    QVERIFY(!mw->isWindowModified());
}

// T-104: Undo back to the saved state leaves the document dirty, and the window is never marked
// modified
void KnownBugsWindow::editMarksWindowModified() {
    const QString path = writeMarkedPdf(fixture(QStringLiteral(".pdf")), 2);
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    IDocument *doc = currentDocument(mw);
    QVERIFY(doc);
    QVERIFY(!mw->isWindowModified());

    actionNamed(mw, QStringLiteral("action.tools.rotateRight"))->trigger();
    QVERIFY(doc->isDirty());
    QEXPECT_FAIL("", "T-104: an edit never sets the window's modified state", Abort);
    QVERIFY(mw->isWindowModified());

    actionNamed(mw, QStringLiteral("action.file.save"))->trigger();
    QTRY_VERIFY(!saveInFlight(mw) && !doc->isDirty());
    QVERIFY(!mw->isWindowModified());
}

void KnownBugsWindow::readingPositionSurvivesSaveAndUndo_data() {
    // Every row rotates page 4 of 4, then reads part way down page 3.
    QTest::addColumn<QString>("op");
    QTest::newRow("save") << QStringLiteral("save");
    // Undo and Redo of that rotation act on a page that is not on screen.
    QTest::newRow("undo") << QStringLiteral("undo");
    QTest::newRow("redo") << QStringLiteral("redo");
}

// T-103: Save, page operations and Undo move the reading position
void KnownBugsWindow::readingPositionSurvivesSaveAndUndo() {
    QFETCH(QString, op);
    const QString path = writeMarkedPdf(fixture(QStringLiteral(".pdf")), 4);
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    IDocument *doc = currentDocument(mw);
    QPdfView *view = pdfView(mw);
    QVERIFY(doc && view);
    QTRY_VERIFY(doc->zoomMode() == ZoomMode::FitInView);
    QAction *next = actionNamed(mw, QStringLiteral("action.view.nextPage"));
    QAction *previous = actionNamed(mw, QStringLiteral("action.view.previousPage"));
    QAction *undo = actionWithShortcut(mw, QKeySequence::Undo);
    QAction *redo = actionWithShortcut(mw, QKeySequence::Redo);
    QVERIFY(next && previous && undo && redo);

    for (int i = 0; i < 3; ++i)
        next->trigger();
    settle();
    QCOMPARE(doc->currentPage(), 3);
    actionNamed(mw, QStringLiteral("action.tools.rotateRight"))->trigger();
    QVERIFY(doc->isDirty());
    if (op == QLatin1String("redo")) {
        undo->trigger();
        QVERIFY(doc->canRedo());
    }
    previous->trigger();
    settle();
    QCOMPARE(doc->currentPage(), 2);
    QScrollBar *vbar = view->verticalScrollBar();
    vbar->setValue(vbar->value() + view->viewport()->height() / 3);
    settle();
    QCOMPARE(doc->currentPage(), 2);

    QRect before;
    QVERIFY(settledCentreMarker(view->viewport(), &before));
    if (op == QLatin1String("save")) {
        actionNamed(mw, QStringLiteral("action.file.save"))->trigger();
        QTRY_VERIFY(!saveInFlight(mw) && !doc->isDirty());
    } else {
        (op == QLatin1String("undo") ? undo : redo)->trigger();
    }
    settle();
    QRect after;
    QVERIFY(settledCentreMarker(view->viewport(), &after));

    const QPoint moved = after.topLeft() - before.topLeft();
    QEXPECT_FAIL("save", "T-103: Save returns the view to the first page", Abort);
    QEXPECT_FAIL("undo", "T-103: Undo jumps to the top of the page", Abort);
    QEXPECT_FAIL("redo", "T-103: Redo jumps to the top of the page", Abort);
    QVERIFY2(std::abs(moved.x()) <= 2 && std::abs(moved.y()) <= 2 && after.size() == before.size(),
             qPrintable(QStringLiteral("marker under the viewport centre was at %1, now %2")
                            .arg(rectText(before), rectText(after))));
}

// T-12: Opening Find moves the toolbar controls and opens the sidebar
void KnownBugsWindow::findAndEscapeLeaveLayoutUnchanged() {
    const QString path = writeMarkedPdf(fixture(QStringLiteral(".pdf")), 2);
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    settle();
    const QList<ToolbarControl> before = mainToolbarControls(mw);
    const QPoint viewBefore = documentViewPos(mw);
    const QSize viewSizeBefore = documentView(mw)->size();
    const bool sidebarBefore = mw->isSidebarVisible();
    // The window is 1,280 px wide, so the search field shows at rest.
    QEXPECT_FAIL("", "T-12: at rest the search field is collapsed to a button", Continue);
    QVERIFY(searchBar->isVisible());

    QTest::keySequence(mw->windowHandle(), QKeySequence::Find);
    QTRY_VERIFY(searchBar->isVisible());
    settle();
    const QList<ToolbarControl> during = mainToolbarControls(mw);
    const QRect viewDuring(documentViewPos(mw), documentView(mw)->size());

    // keyClick delivers Esc before it returns. No wait for the field to hide:
    // at rest it stays shown (checked above), and "after" must match "before".
    QTest::keyClick(mw->windowHandle(), Qt::Key_Escape);
    settle();
    const QList<ToolbarControl> after = mainToolbarControls(mw);

    QEXPECT_FAIL("", "T-12: Find grows the toolbar and moves its controls", Continue);
    QCOMPARE(placements(during, before), placements(before, during));
    QCOMPARE(placements(after, after), placements(before, before));
    QEXPECT_FAIL("", "T-12: Find opens the sidebar and moves the document", Continue);
    QCOMPARE(viewDuring, QRect(viewBefore, viewSizeBefore));
    QEXPECT_FAIL("", "T-12: the sidebar Find opened stays open after Esc", Abort);
    QCOMPARE(mw->isSidebarVisible(), sidebarBefore);
    QCOMPARE(QRect(documentViewPos(mw), documentView(mw)->size()),
             QRect(viewBefore, viewSizeBefore));
}

// T-21: Two Pages is read-only and hides the markup toolbar
void KnownBugsWindow::viewModeSwitchKeepsToolbarsAndDocumentInPlace() {
    const QString path = writeMarkedPdf(fixture(QStringLiteral(".pdf")), 3);
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    actionNamed(mw, QStringLiteral("action.view.markupToolbar"))->trigger();
    settle();
    const QString start = toolbarsAndDocumentY(mw);
    QVERIFY2(start.contains(QLatin1String("MarkupToolbar shown")), qPrintable(start));

    for (const char *mode : {"action.view.singlePage", "action.view.continuous",
                             "action.view.twoPages", "action.view.continuous"}) {
        QAction *a = actionNamed(mw, QString::fromLatin1(mode));
        QVERIFY(a && a->isEnabled());
        a->trigger();
        settle();
        if (qstrcmp(mode, "action.view.twoPages") == 0)
            QEXPECT_FAIL("", "T-21: Two Pages hides the markup toolbar", Abort);
        QCOMPARE(toolbarsAndDocumentY(mw), start);
    }
}

// T-11: The status bar is always shown
void KnownBugsWindow::pdfWindowShowsNoStatusBar() {
    const QString path = writeMarkedPdf(fixture(QStringLiteral(".pdf")), 1);
    MainWindow *mw = openWindow({path});
    QVERIFY(mw);
    settle();
    // Not statusBar(): it creates a bar when there is none, and a new bar
    // reads as hidden until the event loop shows it.
    auto *bar = mw->findChild<QStatusBar *>(QString(), Qt::FindDirectChildrenOnly);
    QEXPECT_FAIL("", "T-11: the status bar is always shown", Abort);
    QVERIFY(!bar || !bar->isVisible());
}

int main(int argc, char **argv) {
    // Settings, recent files and app data go to a throwaway HOME.
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // QSettings defaults to NativeFormat on macOS, which ignores HOME.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    KnownBugsWindow tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_known_bugs_window.moc"
