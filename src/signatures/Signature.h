#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>

namespace trailer {

// A captured signature. Stored on disk as a pair of files under
// AppPaths::signaturesDir():
//
//   <id>.png    PNG with alpha (drawn ink over transparent background)
//   <id>.json   {"label": "...", "created": "...", "alt_text": "..."}
//
// `id` is the filename stem — a UUID-ish timestamp so the folder stays
// sortable and collision-free without depending on the label.
struct Signature {
    QString id;          // filename stem
    QString label;       // user-visible name ("Jeff", "Initials")
    QDateTime createdAt; // populated from the JSON or file mtime
    QString altText;     // screen-reader description (§6.4.3)

    // Absolute path to the PNG file. Empty if the signature hasn't
    // been written yet.
    QString pngPath;
};

} // namespace trailer
