#pragma once

#include "ml/MlScheduler.h"

#include <QObject>

#include <memory>
#include <unordered_map>
#include <vector>

namespace trailer {

class Application;
class IDocument;
class OcrEngine;
class SelectableTextStore;

// Coordinates OCR submissions for the active document of a single
// MainWindow. Holds a shared OcrEngine and a small bookkeeping table
// keyed by (document*, page) so we don't submit the same work twice.
//
// Submission shape (Workstream F):
//   - On every current-page change, submit OCR for the visible page
//     at `VisiblePage` priority. Cancel any pending VisiblePage that
//     no longer matches the new page.
//   - For documents with >1 page, submit ±1 neighbours at `Prefetch`
//     priority. Pending Prefetch for far pages gets cancelled when
//     the user jumps somewhere unrelated.
//   - The explicit Recognize Text… dialog submits work at
//     `UserAction` priority (higher than VisiblePage), with the
//     user's selected pages.
//   - Large-doc guard: documents with pageCount() > 50 skip the
//     automatic VisiblePage / Prefetch enqueue. The Recognize Text
//     dialog stays available; the per-page MainWindow status bar
//     offers an explicit "Recognize text on this page" affordance.
//
// All submissions go through Application::mlScheduler() so the
// scheduler's power policy (run-on-battery, etc.) and priority
// preemption apply uniformly. Cancellation is cooperative — the
// engine polls the token between detector / per-box recognition
// steps.
//
// The controller listens for SelectableTextStore::changed signals
// from each document's store so a manual user action that invalidates
// a page (rotate, crop) automatically re-enqueues the visible page.
class OcrController : public QObject {
    Q_OBJECT
  public:
    explicit OcrController(Application *app, QObject *parent = nullptr);
    ~OcrController() override;

    // Switch which document the controller follows. Pass nullptr when
    // the window has no current document. Cancels any in-flight
    // submissions for the previous document.
    void setDocument(IDocument *doc);
    IDocument *document() const { return m_doc; }

    // Notify the controller that the visible page changed. Submits a
    // VisiblePage OCR for the new page (if not cached + not in flight)
    // and Prefetch for neighbours. Cancels pending submissions that
    // are no longer relevant.
    void onVisiblePageChanged(int page);

    // Explicit Recognize Text… submission, called from
    // RecognizeTextDialog. Submits at UserAction priority. `pages` is
    // a list of 0-based page indices; if `forceRerun` is true, the
    // cache is invalidated for those pages before submission so a doc
    // that "has a text layer in a watermark only" can be re-OCR'd
    // even though hasTextLayer() returns true.
    void submitUserPages(IDocument *doc, std::vector<int> pages, bool forceRerun);

    // Cancel everything we have in flight. Used on window close /
    // doc replace.
    void cancelAll();

    // Page-count threshold above which we skip the automatic visible-
    // page enqueue. Exposed for tests; matches the value in the spec
    // (>50 pages).
    static constexpr int kLargeDocPageThreshold = 50;

    // True when the document's page count exceeds the auto-OCR
    // threshold. The MainWindow uses this to decide whether to show
    // the in-status-bar hint chip ("Text isn't selectable here").
    bool isLargeDoc() const;

  private:
    // Submission state tracked per (doc, page) so duplicate enqueues
    // and cancellations land in O(1).
    struct PendingKey {
        IDocument *doc = nullptr;
        int page = -1;
        bool operator==(const PendingKey &o) const noexcept {
            return doc == o.doc && page == o.page;
        }
    };
    struct PendingKeyHash {
        size_t operator()(const PendingKey &k) const noexcept {
            return std::hash<IDocument *>()(k.doc) ^ (std::hash<int>()(k.page) << 1);
        }
    };

    void submitPage(IDocument *doc, int page, MlPriority priority, bool forceRerun);
    void cancelKey(const PendingKey &key);
    void cancelPagesNotMatching(IDocument *doc, const std::vector<int> &keep);

    Application *m_app;
    // IDocument isn't a QObject, so we can't use QPointer here. The
    // pointer is invalidated only by setDocument(); the MainWindow
    // calls setDocument(nullptr) before destroying a document, so
    // this never dangles in practice.
    IDocument *m_doc = nullptr;
    // shared_ptr (not unique) so worker-thread lambdas can capture
    // a copy by value and keep the engine alive past controller
    // destruction. cancelAll() in the destructor flips every active
    // token, but the worker may still be inside an inference step
    // when the controller frees; the shared_ptr defers OcrEngine
    // destruction until the lambda exits.
    std::shared_ptr<OcrEngine> m_engine;
    std::unordered_map<PendingKey, MlTaskId, PendingKeyHash> m_pending;
};

} // namespace trailer
