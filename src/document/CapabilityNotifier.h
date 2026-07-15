#pragma once

#include <QObject>

namespace trailer {

// Tiny QObject emitter that lets a non-QObject IDocument announce that a
// capability it resolves asynchronously has become known. PdfDocument owns
// one and fires it once the background load (qpdf parse + AcroForm
// detection) completes, so MainWindow can re-run the forms-toolbar
// enable/populate block a moment after open — the owner-requested behaviour
// on PR #63 (the parse must not block the GUI thread at open).
//
// Kept deliberately minimal (one signal). IDocument returns nullptr by
// default; only adapters whose capabilities settle after open hand one back.
class CapabilityNotifier : public QObject {
    Q_OBJECT
  public:
    explicit CapabilityNotifier(QObject *parent = nullptr) : QObject(parent) {}

    // Emit from the owning document (signals are not callable from outside
    // the declaring class, so route through this public shim).
    void notifyChanged() { Q_EMIT capabilitiesChanged(); }

  Q_SIGNALS:
    void capabilitiesChanged();
};

} // namespace trailer
