#pragma once

#include "document/IDocument.h"
#include "recent/RecentFiles.h"

#include <QByteArray>
#include <QString>

namespace trailer {

// Per-file-type "last-closed-of-type wins" defaults. When the user
// opens a file Trailer has never seen before, the persistence layer
// falls through to whichever defaults were captured the last time
// they closed a file of the same type (PDF or Image). Simple,
// predictable; no merging or scoring across past sessions.
//
// Persisted under QSettings (settings.toml is reserved for human-
// editable preferences; this state is opaque). Default-constructed
// instances yield "no captured defaults" — the open path then falls
// through to the hardcoded constructor defaults.
struct DocumentTypeDefault {
    ZoomMode zoomMode = ZoomMode::Custom;
    double zoomFactor = 0.0;
    SidebarMode sidebarMode = SidebarMode::Hidden;
    bool markupToolbarVisible = false;
    QByteArray windowGeometry;
    QByteArray windowState;

    // True when at least one field has been explicitly captured.
    // The open path uses this to decide whether to apply or skip.
    bool hasState() const {
        return zoomMode != ZoomMode::Custom || zoomFactor > 0.0 ||
               sidebarMode != SidebarMode::Hidden || markupToolbarVisible ||
               !windowGeometry.isEmpty() || !windowState.isEmpty();
    }
};

class DocumentTypeDefaults {
  public:
    DocumentTypeDefaults();
    // Test seam — explicit QSettings key prefix so the round-trip
    // tests don't collide with the user's real preferences.
    explicit DocumentTypeDefaults(QString organisation, QString application);

    void load();
    void save() const;

    DocumentTypeDefault forType(DocumentType type) const;
    void setForType(DocumentType type, const DocumentTypeDefault &value);

    // Path under which QSettings stores the blobs. Public so tests
    // can stamp / clear the underlying QSettings group.
    QString organisation() const { return m_organisation; }
    QString application() const { return m_application; }

  private:
    QString m_organisation;
    QString m_application;
    DocumentTypeDefault m_pdf;
    DocumentTypeDefault m_image;
};

} // namespace trailer
