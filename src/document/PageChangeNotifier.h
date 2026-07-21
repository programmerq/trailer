#pragma once

#include <QObject>

namespace trailer {

// Tiny QObject emitter that lets a non-QObject IDocument announce that its
// current page changed. Mirrors CapabilityNotifier: IDocument is deliberately
// NOT a QObject (it is multiply-implemented by value-owned adapters and mixed
// into QWidget-adjacent code), so instead of promoting the whole interface to
// QObject we route the one signal a caller needs through this shim.
//
// PdfDocument owns one and fires it from the QPdfPageNavigator::currentPageChanged
// path (keyboard paging, thumbnail jumps, and continuous-scroll page crossings
// all funnel through the navigator). Consumers — the Sidebar page-sync and the
// MainWindow auto-OCR / missing-model hint re-derivation — connect to it instead
// of polling currentPage() on a timer.
//
// Kept deliberately minimal (one signal carrying the new page index).
// IDocument returns nullptr by default; only adapters whose current page can
// change after open (PdfDocument) hand one back.
class PageChangeNotifier : public QObject {
    Q_OBJECT
  public:
    explicit PageChangeNotifier(QObject *parent = nullptr) : QObject(parent) {}

    // Emit from the owning document (signals are not callable from outside the
    // declaring class, so route through this public shim).
    void notifyPageChanged(int page) { Q_EMIT currentPageChanged(page); }

  Q_SIGNALS:
    void currentPageChanged(int page);
};

} // namespace trailer
