#pragma once

#include <QDockWidget>
#include <QTimer>

#include <vector>

class QListView;
class QListWidget;
class QStackedWidget;
class QTabWidget;

namespace trailer {

class IDocument;
class ThumbnailModel;

class Sidebar : public QDockWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void setDocument(IDocument* doc);
    void refreshThumbnails();
    void refreshAnnotations();

signals:
    void deletePagesRequested(const std::vector<int>& pageIndices);
    void movePageRequested(int from, int to);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onThumbnailActivated(const QModelIndex& index);
    void syncSelectionFromDocument();
    void onAnnotationActivated();

private:
    IDocument* m_doc = nullptr;
    QStackedWidget* m_stack = nullptr;
    QTabWidget* m_tabs = nullptr;
    QListView* m_thumbnails = nullptr;
    QListWidget* m_annotations = nullptr;
    ThumbnailModel* m_model = nullptr;
    QTimer m_pageSyncTimer;
    int m_placeholderIndex = 0;
    int m_tabsIndex = 0;
    bool m_syncingSelection = false;
};

}  // namespace trailer
