#pragma once

#include "signatures/SignatureStore.h"

#include <QDialog>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;

namespace trailer {

// "Manage Signatures" dialog — the entry point for capturing new
// signatures and removing old ones. Shown from Tools > Manage
// Signatures…. Users pick one from the list to preview it; the
// currently-selected signature becomes the "active" one that the
// Sign tool places on the page (persisted via m_store.setActive
// semantics in the future; Phase 5 just returns the selected id).
class SignaturesDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SignaturesDialog(QWidget *parent = nullptr);

    // After exec(): the id of the signature the user picked, or an
    // empty string if none / rejected. Callers use this to pre-select
    // in the Sign tool.
    QString selectedId() const { return m_selectedId; }

  private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onSelectionChanged();

  private:
    void reload();
    QListWidgetItem *itemForId(const QString &id) const;

    SignatureStore m_store;
    std::vector<Signature> m_signatures;
    QString m_selectedId;

    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
};

} // namespace trailer
