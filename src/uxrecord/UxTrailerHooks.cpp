// Trailer-side wiring for the UX recorder: everything that touches
// MainWindow lives here, behind uxrecord::attachToMainWindow(), so the
// rest of the app only carries facade calls. Compiled only when
// TRAILER_ENABLE_UX_RECORDER is ON; attachToMainWindow() is a no-op
// inline in default builds (uxrecord/UxRecord.h).

#include "uxrecord/UxRecord.h"

#include "annotation/Annotation.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/FormToolbar.h"
#include "ui/MainWindow.h"
#include "ui/MarkupToolbar.h"
#include "uxrecord/UxRecorder.h"

#include <QAction>
#include <QDesktopServices>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace trailer {
namespace uxrecord {

namespace {

// How often Trailer document state (page, zoom, view mode, dirty) is
// polled for change events. IDocument is not a QObject, so there is no
// signal to subscribe to; 500 ms keeps page-turn timestamps within
// half a second of reality at negligible cost.
constexpr int kStatePollIntervalMs = 500;

QString stripMnemonic(QString text) {
    text.remove(QLatin1Char('&'));
    return text;
}

QString zoomModeName(ZoomMode mode) {
    switch (mode) {
    case ZoomMode::FitInView:
        return QStringLiteral("fit_in_view");
    case ZoomMode::FitToWidth:
        return QStringLiteral("fit_to_width");
    case ZoomMode::Actual:
        return QStringLiteral("actual");
    case ZoomMode::Custom:
        break;
    }
    return QStringLiteral("custom");
}

QString viewModeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::SinglePage:
        return QStringLiteral("single_page");
    case ViewMode::TwoPages:
        return QStringLiteral("two_pages");
    case ViewMode::Continuous:
        break;
    }
    return QStringLiteral("continuous");
}

QString documentKindName(DocumentType type) {
    switch (type) {
    case DocumentType::Pdf:
        return QStringLiteral("pdf");
    case DocumentType::Image:
        return QStringLiteral("image");
    case DocumentType::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString annotationToolName(AnnotationTool tool) {
    switch (tool) {
    case AnnotationTool::None:
        return QStringLiteral("none");
    case AnnotationTool::Select:
        return QStringLiteral("select");
    case AnnotationTool::Rectangle:
        return QStringLiteral("rectangle");
    case AnnotationTool::Ellipse:
        return QStringLiteral("ellipse");
    case AnnotationTool::Line:
        return QStringLiteral("line");
    case AnnotationTool::Arrow:
        return QStringLiteral("arrow");
    case AnnotationTool::Ink:
        return QStringLiteral("ink");
    case AnnotationTool::Text:
        return QStringLiteral("text");
    case AnnotationTool::Note:
        return QStringLiteral("note");
    case AnnotationTool::Highlight:
        return QStringLiteral("highlight");
    case AnnotationTool::Underline:
        return QStringLiteral("underline");
    case AnnotationTool::StrikeOut:
        return QStringLiteral("strike_out");
    case AnnotationTool::HighlightShape:
        return QStringLiteral("highlight_shape");
    case AnnotationTool::SpeechBubble:
        return QStringLiteral("speech_bubble");
    case AnnotationTool::ZoomLens:
        return QStringLiteral("zoom_lens");
    case AnnotationTool::Signature:
        return QStringLiteral("signature");
    case AnnotationTool::Redaction:
        return QStringLiteral("redaction");
    case AnnotationTool::InstantAlpha:
        return QStringLiteral("instant_alpha");
    case AnnotationTool::SmartLasso:
        return QStringLiteral("smart_lasso");
    }
    return QStringLiteral("unknown");
}

// Snapshot of the state an analyst needs to interpret the next event:
// which document, where in it, at what zoom, with which tool active.
QJsonObject documentSnapshot(IDocument *doc, MarkupToolbar *markup) {
    QJsonObject snapshot;
    if (!doc) {
        snapshot.insert(QStringLiteral("document"), QJsonValue::Null);
        return snapshot;
    }
    snapshot.insert(QStringLiteral("document"), doc->filePath());
    snapshot.insert(QStringLiteral("display_name"), doc->displayName());
    snapshot.insert(QStringLiteral("document_kind"), documentKindName(doc->documentType()));
    snapshot.insert(QStringLiteral("page"), doc->currentPage());
    snapshot.insert(QStringLiteral("page_count"), doc->pageCount());
    snapshot.insert(QStringLiteral("zoom_mode"), zoomModeName(doc->zoomMode()));
    snapshot.insert(QStringLiteral("zoom"), doc->zoomFactor());
    snapshot.insert(QStringLiteral("view_mode"), viewModeName(doc->viewMode()));
    snapshot.insert(QStringLiteral("dirty"), doc->isDirty());
    if (markup) {
        snapshot.insert(QStringLiteral("active_tool"), annotationToolName(markup->activeTool()));
    }
    return snapshot;
}

// Shared mutable state for one window's instrumentation, captured by
// the lambdas below. Owned by a shared_ptr held in each connection's
// closure; dies with the window's connections.
struct WindowState {
    QJsonObject lastSnapshot;
};

} // namespace

void attachToMainWindow(MainWindow *window) {
    UxRecorder *rec = recorder();
    if (!rec || !window) {
        return;
    }

    auto *documentView = window->findChild<DocumentView *>();
    auto *markupToolbar = window->findChild<MarkupToolbar *>();
    auto *formToolbar = window->findChild<FormToolbar *>();
    auto state = std::make_shared<WindowState>();

    // ---- Obvious recording indicator -------------------------------
    // Permanent status-bar chip, mirroring the ML indicator pattern.
    auto *chip = new QLabel(QStringLiteral("● REC"), window);
    chip->setObjectName(QStringLiteral("uxRecorderIndicator"));
    chip->setStyleSheet(QStringLiteral("QLabel { color: #d22; font-weight: bold; }"));
    chip->setToolTip(MainWindow::tr("UX recording in progress.\nSession: %1\nEverything stays "
                                    "on this machine — nothing is uploaded.")
                         .arg(rec->sessionDir()));
    window->statusBar()->addPermanentWidget(chip);

    QObject::connect(rec, &UxRecorder::captureIssue, window, &MainWindow::flashError);
    QObject::connect(rec, &UxRecorder::markerInserted, window, [window](const QString &kind) {
        window->flashStatus(MainWindow::tr("Marker recorded: %1").arg(kind));
    });

    // ---- Recording menu --------------------------------------------
    // A dedicated top-level menu doubles as a second indicator and
    // keeps recorder actions out of the normal menus, which must look
    // identical to a non-recording run.
    QMenu *menu = window->menuBar()->addMenu(MainWindow::tr("◉ Recording"));
    const auto internal = [](QAction *a) {
        a->setProperty("ux_recorder_internal", true);
        return a;
    };

    QAction *frustration = internal(menu->addAction(MainWindow::tr("Insert &Frustration Marker")));
    frustration->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    QObject::connect(frustration, &QAction::triggered, rec,
                     [rec]() { rec->insertMarker(QStringLiteral("frustration")); });

    QAction *unexpected =
        internal(menu->addAction(MainWindow::tr("Insert &Unexpected-Behavior Marker")));
    QObject::connect(unexpected, &QAction::triggered, rec,
                     [rec]() { rec->insertMarker(QStringLiteral("unexpected_behavior")); });

    QAction *important =
        internal(menu->addAction(MainWindow::tr("Insert &Important-Moment Marker")));
    QObject::connect(important, &QAction::triggered, rec,
                     [rec]() { rec->insertMarker(QStringLiteral("important_moment")); });

    QAction *noteMarker = internal(menu->addAction(MainWindow::tr("Insert &Note Marker…")));
    QObject::connect(noteMarker, &QAction::triggered, window, [rec, window]() {
        bool ok = false;
        const QString note =
            QInputDialog::getText(window, MainWindow::tr("Note Marker"),
                                  MainWindow::tr("Describe this moment for the session timeline:"),
                                  QLineEdit::Normal, QString(), &ok);
        if (ok && !note.isEmpty()) {
            rec->insertMarker(QStringLiteral("note"), note);
        }
    });

    menu->addSeparator();

#ifdef Q_OS_MACOS
    // ---- Intentional Preview fallback ------------------------------
    // The instrumented "I'm giving up and using Preview" gesture:
    // records Trailer state + a marker, opens the document in Preview
    // (by bundle id), and lets the platform capture follow the app
    // switch. Trailer has no generic open-externally action to extend,
    // so this is recorder-only UI.
    QAction *handOff = internal(menu->addAction(MainWindow::tr("&Hand Off to Preview")));
    handOff->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y));
    QObject::connect(
        handOff, &QAction::triggered, window, [rec, window, documentView, markupToolbar, state]() {
            IDocument *doc = documentView ? documentView->currentDocument() : nullptr;
            if (!doc) {
                return;
            }
            const QString path = doc->filePath();
            if (path.isEmpty()) {
                window->flashStatus(
                    MainWindow::tr("Save the document before handing off to Preview."));
                return;
            }
            // Flush unsaved edits so Preview shows what the
            // user is looking at (same sync save the
            // auto-save timer performs).
            if (doc->isDirty()) {
                doc->save();
            }
            QJsonObject snapshot = documentSnapshot(doc, markupToolbar);
            snapshot.insert(QStringLiteral("reason"), QStringLiteral("explicit_preview_fallback"));
            rec->recordTrailerEvent(QStringLiteral("preview_fallback_started"), snapshot);
            rec->insertMarker(QStringLiteral("preview_handoff"), path);
            QProcess::startDetached(
                QStringLiteral("/usr/bin/open"),
                {QStringLiteral("-b"), QStringLiteral("com.apple.Preview"), path});
            window->flashStatus(
                MainWindow::tr("Opened in Preview — recording follows the hand-off."));
        });
    // Philosophy: disable + explain rather than popup-and-refuse.
    const auto refreshHandOff = [handOff](IDocument *doc) {
        const bool enabled = doc && !doc->filePath().isEmpty();
        handOff->setEnabled(enabled);
        handOff->setToolTip(enabled ? MainWindow::tr("Open this document in Preview and keep "
                                                     "recording the fallback workflow.")
                                    : MainWindow::tr("Open (and save) a document first — "
                                                     "Preview needs a file on disk."));
    };
    refreshHandOff(documentView ? documentView->currentDocument() : nullptr);
    if (documentView) {
        QObject::connect(documentView, &DocumentView::currentDocumentChanged, handOff,
                         refreshHandOff);
    }
#endif

    if (rec->platformCaptureSupported()) {
        QAction *pause =
            internal(menu->addAction(MainWindow::tr("&Pause Screen && Input Capture")));
        pause->setCheckable(true);
        pause->setChecked(rec->visualCapturePaused());
        QObject::connect(pause, &QAction::toggled, rec,
                         [rec](bool on) { rec->setVisualCapturePaused(on); });
    }

    menu->addSeparator();

    QAction *showFolder = internal(menu->addAction(MainWindow::tr("&Show Session Folder")));
    QObject::connect(showFolder, &QAction::triggered, rec, [rec]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(rec->sessionDir()));
    });

    // ---- Semantic instrumentation ----------------------------------
    // Every QAction that exists at attach time (the full menu/toolbar
    // set — built before the MainWindow constructor returns) reports
    // activations. Actions created later (e.g. the rebuilt Open Recent
    // entries) are not individually instrumented; the document_opened
    // event still records the outcome.
    const auto actions = window->findChildren<QAction *>();
    for (QAction *action : actions) {
        if (action->isSeparator() || action->menu() ||
            action->property("ux_recorder_internal").toBool()) {
            continue;
        }
        QObject::connect(action, &QAction::triggered, rec, [rec, action, state](bool checked) {
            QJsonObject data{
                {QStringLiteral("action"), stripMnemonic(action->text())},
                {QStringLiteral("shortcut"), action->shortcut().toString()},
            };
            if (auto *parentMenu = qobject_cast<QMenu *>(action->parent())) {
                data.insert(QStringLiteral("menu"), stripMnemonic(parentMenu->title()));
            }
            if (action->isCheckable()) {
                data.insert(QStringLiteral("checked"), checked);
            }
            // Poller snapshot from just before the action fired —
            // "state immediately before important actions".
            if (!state->lastSnapshot.isEmpty()) {
                data.insert(QStringLiteral("state_before"), state->lastSnapshot);
            }
            rec->recordTrailerEvent(QStringLiteral("action_triggered"), data);
        });
    }

    if (markupToolbar) {
        QObject::connect(markupToolbar, &MarkupToolbar::activeToolChanged, rec,
                         [rec](AnnotationTool tool) {
                             rec->recordTrailerEvent(
                                 QStringLiteral("tool_selected"),
                                 QJsonObject{{QStringLiteral("tool"), annotationToolName(tool)}});
                         });
    }
    if (formToolbar) {
        QObject::connect(formToolbar, &FormToolbar::toolChanged, rec,
                         [rec](AnnotationTool tool, const QString &) {
                             rec->recordTrailerEvent(
                                 QStringLiteral("form_tool_selected"),
                                 QJsonObject{{QStringLiteral("tool"), annotationToolName(tool)}});
                         });
    }

    if (documentView) {
        QObject::connect(documentView, &DocumentView::currentDocumentChanged, rec,
                         [rec, markupToolbar, state](IDocument *doc) {
                             state->lastSnapshot = documentSnapshot(doc, markupToolbar);
                             rec->recordTrailerEvent(QStringLiteral("document_focused"),
                                                     state->lastSnapshot);
                         });
        QObject::connect(
            documentView, &DocumentView::documentAboutToBeRemoved, rec, [rec](IDocument *doc) {
                rec->recordTrailerEvent(
                    QStringLiteral("document_closed"),
                    QJsonObject{{QStringLiteral("document"), doc ? doc->filePath() : QString()}});
            });

        // Page / zoom / view-mode / dirty transitions, by polling (see
        // kStatePollIntervalMs). Emits only the fields that changed
        // plus the full snapshot for context.
        auto *poll = new QTimer(window);
        poll->setInterval(kStatePollIntervalMs);
        QObject::connect(
            poll, &QTimer::timeout, window, [rec, documentView, markupToolbar, state]() {
                IDocument *doc = documentView->currentDocument();
                QJsonObject snapshot = documentSnapshot(doc, markupToolbar);
                if (snapshot == state->lastSnapshot) {
                    return;
                }
                QJsonArray changed;
                for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
                    if (state->lastSnapshot.value(it.key()) != it.value()) {
                        changed.append(it.key());
                    }
                }
                state->lastSnapshot = snapshot;
                rec->recordTrailerEvent(QStringLiteral("document_state_changed"),
                                        QJsonObject{{QStringLiteral("changed"), changed},
                                                    {QStringLiteral("state"), snapshot}});
            });
        poll->start();
    }

    rec->recordTrailerEvent(
        QStringLiteral("window_attached"),
        QJsonObject{{QStringLiteral("window_title"), window->windowTitle()},
                    {QStringLiteral("actions_instrumented"), static_cast<int>(actions.size())}});
}

} // namespace uxrecord
} // namespace trailer
