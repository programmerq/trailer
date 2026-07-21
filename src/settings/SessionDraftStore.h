#pragma once

#include "annotation/Annotation.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace trailer {

// One document within a kept window (macOS "Quit and Keep Windows").
//
// A saved, clean document is stored as a lightweight PATH reference that
// the existing open path reopens from disk. An unsaved or untitled
// document is stored as a DRAFT: the document's raw encoded BYTES (PNG for
// image documents) live in a blob file beside the manifest, so its content
// survives a relaunch byte-for-byte even though it was never saved to a
// user-chosen file. A PDF with unsaved ANNOTATIONS (no structural page
// edits) is stored as an ANNOTATED_PATH: the on-disk file plus a JSON
// payload of its unsaved annotations, so on restore the file reopens and
// the annotations re-apply as individually editable objects, still dirty.
// See docs/decision-records/2026-07-16-quit-and-keep-windows.md
// (D1 → app-managed draft store; PDF annotation-persistence refinement).
// A PDF with STRUCTURAL page edits (rotate/delete/move/crop/insert) is
// stored as a STRUCTURAL_DRAFT: the edited-PDF blob bytes (the qpdf command
// list cannot be serialized — the commands hold live in-memory object
// handles) live in a blob file beside the manifest, and the ORIGINAL on-disk
// path is carried alongside so Save re-associates to it. On restore the
// original reopens, the blob is loaded into a private editor copy, and the
// doc is marked dirty with the original file untouched.
// See docs/backlog/2026-07-19-structural-pdf-keep-fidelity.md.
struct SessionDocDescriptor {
    enum class Kind { Path, Draft, AnnotatedPath, StructuralDraft };

    Kind kind = Kind::Path;

    // Kind::Path — the on-disk file to reopen.
    // Kind::AnnotatedPath — the on-disk file to reopen before re-applying
    // the unsaved `annotations` below (marked dirty on restore).
    QString path;

    // Kind::AnnotatedPath — the document's UNSAVED annotations, serialized
    // to the manifest as JSON and restored as editable objects. Empty for
    // every other kind.
    QList<Annotation> annotations;

    // Kind::Draft — the encoded document bytes plus restore metadata.
    // `bytes` is the exact blob the store round-trips byte-for-byte;
    // `format` names the encoding (e.g. "png"). `untitled` preserves the
    // isUntitled() flag; `originalPath` is the on-disk file a titled-but-
    // dirty document edits (empty for a genuinely untitled document).
    //
    // Kind::StructuralDraft reuses `bytes` (the edited-PDF blob), `format`
    // ("pdf"), and `originalPath` (the original file to reopen + re-associate
    // Save to). `untitled`/`devicePixelRatio`/`captureOrigin` do not apply.
    //
    // `devicePixelRatio` and `captureOrigin` preserve the HiDPI restore
    // state: a raster encoding (PNG) does NOT carry Qt's devicePixelRatio,
    // so an unsaved Retina screenshot (dpr 2.0, capture-origin) would
    // otherwise restore at dpr 1.0 — double its logical size and losing the
    // Actual-Size zoom default. We persist both explicitly and re-apply
    // them on restore. See the #76 HiDPI work + the decision record.
    QByteArray bytes;
    QString format = QStringLiteral("png");
    bool untitled = false;
    QString originalPath;
    double devicePixelRatio = 1.0;
    bool captureOrigin = false;
};

// One kept window: the ordered documents it held at quit.
struct SessionWindowDescriptor {
    QList<SessionDocDescriptor> docs;
};

// App-managed draft store for macOS "Quit and Keep Windows". Serializes
// the open-window set — including the bytes of unsaved/untitled documents —
// to a Trailer-owned directory under the application data dir, and
// rehydrates it on the next launch. Fully cross-platform and headless-
// testable: it deals only in descriptors + bytes and never touches a GUI.
//
// The store is a manifest (JSON) plus one blob file per draft document.
// It is consumed on restore and on a clean Normal quit — clear() removes
// the whole directory so a stale session never resurfaces.
class SessionDraftStore {
  public:
    // Default: store under AppPaths::sessionDraftsDir(). The explicit-dir
    // constructor is for tests (point at a throwaway directory).
    SessionDraftStore();
    explicit SessionDraftStore(QString storeDir);

    // Serialize `windows` to the store, replacing any previous session.
    // Returns false if the store directory could not be written.
    bool save(const QList<SessionWindowDescriptor> &windows) const;

    // Parse the store into descriptors, loading each draft blob's bytes.
    // Returns an empty list when there is no session (or it is unreadable).
    QList<SessionWindowDescriptor> restore() const;

    // True iff a readable kept-windows session with at least one window is
    // present. Cheap manifest-presence check used at launch.
    bool hasSession() const;

    // Remove the entire store directory. Idempotent.
    void clear() const;

    QString storeDir() const { return m_dir; }

  private:
    QString manifestPath() const;

    QString m_dir;
};

} // namespace trailer
