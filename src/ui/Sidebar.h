#pragma once

#include <QDockWidget>
#include <QTimer>

class QListView;
class QStackedWidget;

namespace trailer {

class IDocument;
class ThumbnailModel;

class Sidebar : public QDockWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void setDocument(IDocument* doc);
    void refreshThumbnails();

private slots:
    void onThumbnailActivated(const QModelIndex& index);
    void syncSelectionFromDocument();

private:
    IDocument* m_doc = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListView* m_thumbnails = nullptr;
    ThumbnailModel* m_model = nullptr;
    QTimer m_pageSyncTimer;
    int m_placeholderIndex = 0;
    int m_thumbnailsIndex = 0;
    bool m_syncingSelection = false;
};

}  // namespace trailer
