#pragma once

#include "Signature.h"

#include <QImage>
#include <QString>
#include <vector>

namespace trailer {

// Persists captured signatures under AppPaths::signaturesDir() (or a
// caller-provided directory for tests).
//
// Layout:
//   <dir>/sig_<timestamp>.png
//   <dir>/sig_<timestamp>.json
//
// The JSON sidecar carries label / created / alt_text. If a JSON is
// missing, loadAll() synthesises one from the PNG's file mtime and
// filename. This makes it trivial for users to drop a PNG into the
// folder manually and still see it in the Sign picker.
class SignatureStore {
  public:
    SignatureStore();                     // uses AppPaths::signaturesDir()
    explicit SignatureStore(QString dir); // for tests

    // Scan the directory and return the signatures, sorted newest-first.
    std::vector<Signature> loadAll() const;

    // Save a new signature. The image is written as PNG with alpha
    // preserved. Returns the populated Signature (including id and
    // pngPath) on success, or an empty Signature (id.isEmpty()) on
    // failure. The id is derived from the current UTC timestamp plus
    // a monotonic suffix to avoid collisions when two signatures are
    // saved in the same millisecond.
    Signature add(const QImage &image, const QString &label, const QString &altText = {});

    // Delete both the PNG and the JSON sidecar for this id. Returns
    // true if either file existed and was removed.
    bool remove(const QString &id);

    QString directory() const { return m_dir; }

  private:
    QString m_dir;
    // Monotonic counter so saves within the same millisecond stay
    // unique even after clock resolution rounds them together.
    mutable quint64 m_seq = 0;
};

} // namespace trailer
