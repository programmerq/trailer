#pragma once

#include "annotation/Annotation.h"

#include <QRectF>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <vector>

class QPDF;
class QThread;

namespace trailer {

// Encryption options for password-protecting a PDF on save. Mirrors
// the flag set in the PDF spec but named for humans. An empty
// userPassword means "readers can open without a password" — in that
// case the ownerPassword still gates permission changes, which is how
// you ship a document that anyone can read but not (say) print.
struct EncryptionOptions {
    QString userPassword;
    QString ownerPassword; // empty → derived from userPassword
    bool allowPrint = true;
    bool allowHighResPrint = true; // only meaningful if allowPrint
    bool allowModify = true;
    bool allowExtract = true;
    bool allowAnnotate = true;
    bool allowFormFilling = true;
    bool allowAccessibility = true;
};

// A single AcroForm field as extracted from a PDF document. The id is
// a stable index into the list returned by PdfEditor::readFormFields()
// for a given load(); use it when calling setFormFieldValue().
//
// Coordinates are in PDF points (1/72 inch), bottom-left origin
// (the PDF convention), so the overlay layer converts to screen
// coords using each page's rendered size.
enum class FormFieldType { Text, Checkbox, RadioButton, Dropdown, Unknown };

struct FormField {
    int id = -1;
    int page = 0;
    QRectF rectPts; // bottom-left origin, PDF points
    FormFieldType type = FormFieldType::Unknown;
    QString name;        // fully-qualified internal name (/T hierarchy)
    QString label;       // /TU alternative name (shown to the user)
    QString value;       // current value; "Yes"/"Off" for checkboxes
    QStringList options; // non-empty for Dropdown fields
    bool readOnly = false;
    bool required = false;
    bool multiline = false;  // text fields only
    bool isPassword = false; // text fields only — mask input in the UI
};

class PdfEditor {
  public:
    PdfEditor();
    ~PdfEditor();

    PdfEditor(const PdfEditor &) = delete;
    PdfEditor &operator=(const PdfEditor &) = delete;

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    int pageCount() const;

    void rotatePage(int pageIndex, int degreesClockwise);
    void deletePages(std::vector<int> pageIndices);
    void movePage(int from, int to);
    bool insertPagesFrom(const QString &sourcePath, int insertAtIndex);
    bool extractPages(const std::vector<int> &pageIndices, const QString &destPath) const;

    // Cropping. Margins are in PDF points, measured inward from each edge of the
    // current MediaBox. Returns false if the resulting rectangle would be invalid.
    bool cropPage(int pageIndex, double leftPts, double topPts, double rightPts, double bottomPts);

    // Append in-memory annotations to each page's /Annots array. Caller
    // supplies doc-native coords (PDF points, top-left origin); the writer
    // flips to PDF convention (bottom-left) using each page's MediaBox.
    // Signature annotations are skipped here — they must be burned into
    // the page content stream via flattenSignatures() so they remain
    // visible in readers that don't render annotation appearances.
    bool writeAnnotations(const std::vector<Annotation> &annotations);

    // Flatten every Signature-typed annotation into its page's content
    // stream by embedding the referenced PNG as an image XObject and
    // emitting `q cm Do Q` drawing commands. The annotations themselves
    // are not written to /Annots — the rasterised stamp is the
    // permanent visual. Callers invoke this once, immediately before
    // save() or exportWithPassword(). Returns true when all signatures
    // were embedded (an empty signature list is success).
    bool flattenSignatures(const std::vector<Annotation> &annotations);

    // For every page that has at least one Redaction-typed annotation,
    // render the current page contents to a raster image, paint opaque
    // black over each redacted rectangle, and replace the page's
    // content stream with just that image. The underlying text /
    // glyph operators are discarded, so the redacted content is
    // actually destroyed — not merely visually hidden. Per §6.11.6
    // Trailer is not a defence-grade tool; the caller should warn
    // users that non-page-content metadata (bookmarks, attached
    // files, encrypted layers) is not touched by this pass.
    //
    // Returns true when all affected pages were rasterised (empty
    // redaction list is success). Non-redaction annotations are
    // untouched.
    bool applyRedactions(const std::vector<Annotation> &annotations);

    // Parse /Annots arrays on every page and return an in-memory representation
    // in doc-native coords (top-left origin). Only the subtypes this class
    // writes are recognised; unknown subtypes are skipped silently.
    std::vector<Annotation> readAnnotations() const;

    // AcroForm field access (Phase 5).
    // Returns true if the document has any AcroForm fields — cheap check
    // before calling the more expensive readFormFields().
    bool hasFormFields() const;
    // Parse the document's AcroForm tree and return every leaf field.
    // Text, Checkbox, RadioButton, and Dropdown types are fully decoded;
    // PushButton and unknown types are skipped.
    std::vector<FormField> readFormFields() const;
    // Set the /V entry (and trigger /NeedAppearances) for the field
    // at index `id` in the readFormFields() list. Returns false if the
    // id is out of range, the field is read-only, or the document is
    // invalid. For checkboxes pass "Yes" or "Off".
    bool setFormFieldValue(int id, const QString &value);

    bool save(const QString &path);
    // Overload that writes an AES-256 (R6) password-protected PDF.
    // Callers that don't need encryption should use the one-arg form —
    // it's binary-identical output otherwise.
    bool save(const QString &path, const EncryptionOptions &enc);

    // Write a linearized + compressed version of the document. Streams
    // are re-compressed, object streams are regenerated, and the file
    // is linearized for fast web viewing. The output is not guaranteed
    // to be smaller than the input (already-optimised inputs can even
    // grow slightly), but for typical PDFs the reduction is
    // substantial. Does not modify the in-memory state.
    bool saveReduced(const QString &path);

    // After load(): true if the backing PDF required a password to
    // open. Phase 5 callers prompt the user when this is the case.
    bool isEncrypted() const { return m_encrypted; }

    // Attempt to unlock an already-loaded (but password-gated) PDF.
    // Returns true once the document is accessible. Idempotent for
    // already-unencrypted documents. Wrong password keeps the editor
    // in the loaded-but-locked state.
    bool unlock(const QString &password);

    QPDF *qpdf() { return m_qpdf.get(); }

    // --- Test instrumentation (always compiled, negligible cost) ---
    // These counters exist purely so the structural perf tests
    // (tests/test_perf_lazy_open.cpp) can assert the P0 startup-hang
    // deferrals without touching wall-clock time:
    //   * s_parseCount     — bumped once per qpdf processFile parse in
    //                        load(); proves the second full-file parse is
    //                        deferred off the synchronous open path.
    //   * s_annotationPageVisits — bumped once per page walked inside
    //                        readAnnotations(); proves the all-pages
    //                        annotation sweep does not run at open.
    //   * s_annotationSweepThread — records QThread::currentThread() the
    //                        last time readAnnotations() ran, so the perf
    //                        test can prove the deferred sweep executes on
    //                        a worker thread (NOT the GUI/caller thread),
    //                        the deterministic liveness/ordering proxy for
    //                        the P0 fix.
    // Not part of the production contract; safe to ignore outside tests.
    static int parseCount() { return s_parseCount.load(std::memory_order_relaxed); }
    static int annotationPageVisits() {
        return s_annotationPageVisits.load(std::memory_order_relaxed);
    }
    static QThread *annotationSweepThread() {
        return s_annotationSweepThread.load(std::memory_order_relaxed);
    }
    static void resetInstrumentation() {
        s_parseCount.store(0, std::memory_order_relaxed);
        s_annotationPageVisits.store(0, std::memory_order_relaxed);
        s_annotationSweepThread.store(nullptr, std::memory_order_relaxed);
    }

  private:
    // Test instrumentation counters (see the accessors above).
    static std::atomic<int> s_parseCount;
    static std::atomic<int> s_annotationPageVisits;
    static std::atomic<QThread *> s_annotationSweepThread;

    bool saveImpl(const QString &path, const EncryptionOptions *enc);

    std::unique_ptr<QPDF> m_qpdf;
    std::vector<std::unique_ptr<QPDF>> m_sources;
    bool m_valid = false;
    bool m_encrypted = false;
    QString m_path; // remembered for unlock() retry
};

} // namespace trailer
