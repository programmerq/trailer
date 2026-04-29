#pragma once

#include "document/IDocument.h"

#include <QActionGroup>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <memory>

class QAction;
class QMenu;

namespace trailer {

class AnimationBar;
class Application;
class DocumentView;
class Inspector;
class FormToolbar;
class Magnifier;
class MarkupToolbar;
class SearchBar;
class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Application* app, QWidget* parent = nullptr);

    void addDocument(std::unique_ptr<IDocument> document);
    int documentCount() const;

    // Lightweight status-bar feedback. Replaces operation-failure
    // QMessageBox::warning calls so the user is not punched in the
    // face by a modal every time a crop/save/export fails. Errors
    // carry a longer timeout (12 s) so a user with their eyes on the
    // document still has time to notice; success / neutral messages
    // fade quickly. The error helper prefixes the message with a
    // warning glyph; the success helper with a check.
    void flashError(const QString& message);
    void flashSuccess(const QString& message);
    void flashStatus(const QString& message);

public slots:
    void rebuildRecentMenu();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpen();
    void onSave();
    void onSaveAs();
    void onRotateLeft();
    void onRotateRight();
    void onFlipHorizontal();
    void onFlipVertical();
    void onAdjustSize();
    void onAdjustColour();
    void onRemoveBackground();
    void onInstantAlpha();
    void onSmartLasso();
    void onRecognizeText();
    void onExportAs();
    void onExportPasswordProtected();
    void onReduceFileSize();
    void onTakeScreenshot();
    void onCropImage();
    void onInsertPages();
    void onCropPages();
    void onAbout();
    void onAutoFillCurrentForm();
    void onManageMyCard();
    void onManageSignatures();
    void onSignHere();
    void onCurrentDocumentChanged(IDocument* doc);
    // Invoked whenever the active document's annotation store mutates
    // (add / remove / update / undo / redo). Refreshes the window
    // title, dirty marker, and Undo/Redo action state. Must be a
    // member function — not a lambda — so the Qt::UniqueConnection
    // flag in the connect() call actually takes effect.
    void onActiveAnnotationStoreChanged();

private:
    void buildMenus();
    void buildEditMenu(QMenu* editMenu);
    void buildViewMenu(QMenu* viewMenu);
    void buildToolsMenu(QMenu* toolsMenu);
    // Shows the one-time "redaction is not defence-grade" warning
    // (DESIGN §6.11.6) the first time the user activates the
    // Redaction tool. Returns true if the user either already
    // acknowledged or accepts now; false if they cancel. On accept,
    // persists the acknowledgement in the Application settings so
    // the modal never reappears.
    bool confirmRedactionFirstUse();
    // Generalised one-time warning. `key` selects a flag in
    // Settings::firstUseAcknowledged; once the user accepts, it is
    // remembered across sessions and the dialog never shows again.
    // `acceptText` is the label of the accept button — defaults to
    // the standard "OK" if empty.
    bool confirmFirstUse(const QString& key, const QString& title,
                         const QString& body,
                         const QString& acceptText = QString());
    void syncViewModeActions(IDocument* doc);
    void showSearchBar();
    void hideSearchBar();
    void updateTitleForDocument(IDocument* doc);
    void updateUndoRedoActions(IDocument* doc);
    int selectedPageForEdit(IDocument* doc) const;

    Application* m_app;
    DocumentView* m_documentView = nullptr;
    AnimationBar* m_animationBar = nullptr;
    Inspector* m_inspector = nullptr;
    Magnifier* m_magnifier = nullptr;
    MarkupToolbar* m_markupToolbar = nullptr;
    FormToolbar* m_formToolbar = nullptr;
    SearchBar* m_searchBar = nullptr;
    Sidebar* m_sidebar = nullptr;
    QMenu* m_recentMenu = nullptr;

    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_rotateLeftAction = nullptr;
    QAction* m_rotateRightAction = nullptr;
    QAction* m_flipHorizontalAction = nullptr;
    QAction* m_flipVerticalAction = nullptr;
    QAction* m_adjustSizeAction = nullptr;
    QAction* m_adjustColourAction = nullptr;
    QAction* m_removeBackgroundAction = nullptr;
    QAction* m_instantAlphaAction = nullptr;
    QAction* m_smartLassoAction = nullptr;
    QAction* m_recognizeTextAction = nullptr;
    QAction* m_exportAsAction = nullptr;
    QAction* m_exportPasswordProtectedAction = nullptr;
    QAction* m_reduceFileSizeAction = nullptr;
    QAction* m_screenshotAction = nullptr;
    QAction* m_cropImageAction = nullptr;
    QAction* m_insertPagesAction = nullptr;
    QAction* m_cropPagesAction = nullptr;
    QAction* m_fillFormsAction = nullptr;
    // Documents we've already auto-enabled Fill Forms for. Tracked so
    // that toggling Fill Forms off, switching docs, and switching back
    // does not re-enable it. Pointers may dangle when docs are closed,
    // which is harmless: dangling entries are never dereferenced.
    QSet<const IDocument*> m_autoEnabledFormDocs;
    // Same once-per-doc tracking for the markup toolbar's auto-show
    // behaviour. The toolbar starts hidden; the first time a document
    // that supports annotations becomes current, we show it. After that,
    // the user's explicit hide/show choice is sticky for that document.
    QSet<const IDocument*> m_autoShownMarkupDocs;
    QAction* m_autoFillFormAction = nullptr;
    QAction* m_myCardAction = nullptr;
    QAction* m_manageSignaturesAction = nullptr;
    QAction* m_printAction = nullptr;
    QAction* m_findAction = nullptr;
    QAction* m_findNextAction = nullptr;
    QAction* m_findPreviousAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_zoomActualAction = nullptr;
    QAction* m_zoomFitAction = nullptr;
    QAction* m_magnifierAction = nullptr;
    QAction* m_markupToolbarAction = nullptr;
    QAction* m_formToolbarAction = nullptr;
    QAction* m_inspectorAction = nullptr;

    QActionGroup* m_viewModeGroup = nullptr;
    QAction* m_singlePageAction = nullptr;
    QAction* m_twoPagesAction = nullptr;
    QAction* m_continuousAction = nullptr;

    QAction* m_previousPageAction = nullptr;
    QAction* m_nextPageAction = nullptr;
};

}  // namespace trailer
