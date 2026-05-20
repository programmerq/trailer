#include "SignaturesDialog.h"

#include "SignatureCaptureDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace trailer {

SignaturesDialog::SignaturesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Manage Signatures"));
    resize(560, 360);

    auto *outer = new QHBoxLayout(this);

    // Left: list + add/remove buttons.
    auto *leftCol = new QVBoxLayout;
    m_list = new QListWidget(this);
    leftCol->addWidget(m_list, 1);

    auto *listButtons = new QHBoxLayout;
    m_addButton = new QPushButton(tr("Add…"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton->setEnabled(false);
    listButtons->addWidget(m_addButton);
    listButtons->addWidget(m_removeButton);
    leftCol->addLayout(listButtons);
    outer->addLayout(leftCol, 1);

    // Right: preview + OK/Cancel.
    auto *rightCol = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(300, 200);
    m_preview->setStyleSheet(QStringLiteral("background: white; border: 1px solid gray;"));
    m_preview->setText(tr("No signature selected."));
    rightCol->addWidget(m_preview, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rightCol->addWidget(buttons);
    outer->addLayout(rightCol, 1);

    connect(m_addButton, &QPushButton::clicked, this, &SignaturesDialog::onAddClicked);
    connect(m_removeButton, &QPushButton::clicked, this, &SignaturesDialog::onRemoveClicked);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            &SignaturesDialog::onSelectionChanged);

    reload();
}

void SignaturesDialog::reload() {
    m_list->clear();
    m_signatures = m_store.loadAll();
    for (const Signature &s : m_signatures) {
        auto *item = new QListWidgetItem(s.label, m_list);
        item->setData(Qt::UserRole, s.id);
        item->setToolTip(QLocale().toString(s.createdAt, QLocale::ShortFormat));
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    } else {
        m_preview->clear();
        m_preview->setText(tr("No signatures yet. Click Add to capture one."));
    }
    m_removeButton->setEnabled(m_list->count() > 0);
}

QListWidgetItem *SignaturesDialog::itemForId(const QString &id) const {
    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() == id)
            return it;
    }
    return nullptr;
}

void SignaturesDialog::onAddClicked() {
    SignatureCaptureDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QImage img = dialog.image();
    if (img.isNull())
        return;
    const Signature added = m_store.add(img, dialog.label());
    if (added.id.isEmpty()) {
        QMessageBox::warning(this, tr("Manage Signatures"), tr("Could not save the signature."));
        return;
    }
    reload();
    if (auto *it = itemForId(added.id)) {
        m_list->setCurrentItem(it);
    }
}

void SignaturesDialog::onRemoveClicked() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    const auto choice =
        QMessageBox::question(this, tr("Remove Signature"),
                              tr("Remove \"%1\"? This cannot be undone.").arg(item->text()));
    if (choice != QMessageBox::Yes)
        return;
    m_store.remove(id);
    reload();
}

void SignaturesDialog::onSelectionChanged() {
    auto *item = m_list->currentItem();
    if (!item) {
        m_preview->clear();
        m_selectedId.clear();
        return;
    }
    m_selectedId = item->data(Qt::UserRole).toString();
    for (const Signature &s : m_signatures) {
        if (s.id != m_selectedId)
            continue;
        QImageReader reader(s.pngPath);
        QImage img = reader.read();
        if (img.isNull()) {
            m_preview->setText(tr("(Preview unavailable)"));
            return;
        }
        m_preview->setPixmap(QPixmap::fromImage(img).scaled(m_preview->size(), Qt::KeepAspectRatio,
                                                            Qt::SmoothTransformation));
        break;
    }
}

} // namespace trailer
