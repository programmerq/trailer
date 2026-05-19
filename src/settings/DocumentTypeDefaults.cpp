#include "DocumentTypeDefaults.h"

#include <QSettings>
#include <QString>

namespace trailer {

namespace {

const char *zoomModeKey(ZoomMode m) {
    switch (m) {
    case ZoomMode::Custom:
        return "custom";
    case ZoomMode::FitInView:
        return "fit_in_view";
    case ZoomMode::FitToWidth:
        return "fit_to_width";
    case ZoomMode::Actual:
        return "actual";
    }
    return "custom";
}

ZoomMode zoomModeFromKey(const QString &key) {
    if (key == QLatin1String("fit_in_view"))
        return ZoomMode::FitInView;
    if (key == QLatin1String("fit_to_width"))
        return ZoomMode::FitToWidth;
    if (key == QLatin1String("actual"))
        return ZoomMode::Actual;
    return ZoomMode::Custom;
}

const char *sidebarModeKey(SidebarMode m) {
    switch (m) {
    case SidebarMode::Hidden:
        return "hidden";
    case SidebarMode::Pages:
        return "pages";
    case SidebarMode::SearchResults:
        return "search_results";
    case SidebarMode::TableOfContents:
        return "table_of_contents";
    case SidebarMode::HighlightsAndNotes:
        return "highlights_and_notes";
    }
    return "hidden";
}

SidebarMode sidebarModeFromKey(const QString &key) {
    if (key == QLatin1String("pages"))
        return SidebarMode::Pages;
    if (key == QLatin1String("search_results"))
        return SidebarMode::SearchResults;
    if (key == QLatin1String("table_of_contents"))
        return SidebarMode::TableOfContents;
    if (key == QLatin1String("highlights_and_notes"))
        return SidebarMode::HighlightsAndNotes;
    return SidebarMode::Hidden;
}

QString groupForType(DocumentType type) {
    switch (type) {
    case DocumentType::Pdf:
        return QStringLiteral("DocumentTypeDefaults/pdf");
    case DocumentType::Image:
        return QStringLiteral("DocumentTypeDefaults/image");
    case DocumentType::Unknown:
        break;
    }
    return {};
}

void loadOne(QSettings &settings, const QString &group, DocumentTypeDefault &out) {
    settings.beginGroup(group);
    out.zoomMode = zoomModeFromKey(settings.value(QStringLiteral("zoom_mode")).toString());
    out.zoomFactor = settings.value(QStringLiteral("zoom_factor"), 0.0).toDouble();
    out.sidebarMode = sidebarModeFromKey(settings.value(QStringLiteral("sidebar_mode")).toString());
    out.markupToolbarVisible =
        settings.value(QStringLiteral("markup_toolbar_visible"), false).toBool();
    out.windowGeometry = settings.value(QStringLiteral("window_geometry")).toByteArray();
    out.windowState = settings.value(QStringLiteral("window_state")).toByteArray();
    settings.endGroup();
}

void saveOne(QSettings &settings, const QString &group, const DocumentTypeDefault &in) {
    settings.beginGroup(group);
    settings.setValue(QStringLiteral("zoom_mode"), QString::fromLatin1(zoomModeKey(in.zoomMode)));
    settings.setValue(QStringLiteral("zoom_factor"), in.zoomFactor);
    settings.setValue(QStringLiteral("sidebar_mode"),
                      QString::fromLatin1(sidebarModeKey(in.sidebarMode)));
    settings.setValue(QStringLiteral("markup_toolbar_visible"), in.markupToolbarVisible);
    settings.setValue(QStringLiteral("window_geometry"), in.windowGeometry);
    settings.setValue(QStringLiteral("window_state"), in.windowState);
    settings.endGroup();
}

} // namespace

DocumentTypeDefaults::DocumentTypeDefaults()
    : DocumentTypeDefaults(QStringLiteral("Trailer"), QStringLiteral("Trailer")) {}

DocumentTypeDefaults::DocumentTypeDefaults(QString organisation, QString application)
    : m_organisation(std::move(organisation)), m_application(std::move(application)) {}

void DocumentTypeDefaults::load() {
    QSettings settings(m_organisation, m_application);
    loadOne(settings, groupForType(DocumentType::Pdf), m_pdf);
    loadOne(settings, groupForType(DocumentType::Image), m_image);
}

void DocumentTypeDefaults::save() const {
    QSettings settings(m_organisation, m_application);
    saveOne(settings, groupForType(DocumentType::Pdf), m_pdf);
    saveOne(settings, groupForType(DocumentType::Image), m_image);
    settings.sync();
}

DocumentTypeDefault DocumentTypeDefaults::forType(DocumentType type) const {
    switch (type) {
    case DocumentType::Pdf:
        return m_pdf;
    case DocumentType::Image:
        return m_image;
    case DocumentType::Unknown:
        break;
    }
    return {};
}

void DocumentTypeDefaults::setForType(DocumentType type, const DocumentTypeDefault &value) {
    switch (type) {
    case DocumentType::Pdf:
        m_pdf = value;
        return;
    case DocumentType::Image:
        m_image = value;
        return;
    case DocumentType::Unknown:
        return;
    }
}

} // namespace trailer
