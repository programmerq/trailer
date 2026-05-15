#include "ModelManagerDialog.h"

#include "app/Application.h"
#include "settings/Settings.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <optional>

namespace trailer {

namespace ModelPolicy {

QString flagKey(ModelId id) {
    return QStringLiteral("ml_never_download_") + modelIdKey(id);
}

bool isNeverDownload(Application *app, ModelId id) {
    return app->settings().firstUseAcknowledged(flagKey(id));
}

void setNeverDownload(Application *app, ModelId id, bool enabled) {
    Settings &s = app->settings();
    s.setFirstUseAcknowledged(flagKey(id), enabled);
    s.save();
}

} // namespace ModelPolicy

namespace {

QList<ModelId> allManagedModelIds() {
    return {ModelId::U2NetP,
            ModelId::BiRefNetLite,
            ModelId::MobileSamEncoder,
            ModelId::MobileSamDecoder,
            ModelId::PpOcrDetector,
            ModelId::PpOcrDirection,
            ModelId::PpOcrRecognizerLatin,
            ModelId::PpOcrRecognizerCjk};
}

QString formatSize(qint64 bytes) {
    if (bytes <= 0)
        return QObject::tr("Unknown");
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat);
}

qint64 totalSizeBytes(const ModelRegistry &registry, const QList<ModelId> &ids) {
    qint64 total = 0;
    for (ModelId id : ids) {
        const ModelSpec spec = registry.spec(id);
        if (spec.id == id && spec.size > 0)
            total += spec.size;
    }
    return total;
}

bool anyNeverDownloadEnabled(Application *app, const QList<ModelId> &ids) {
    for (ModelId id : ids) {
        if (ModelPolicy::isNeverDownload(app, id))
            return true;
    }
    return false;
}

bool isDownloadable(const ModelSpec &spec) {
    return !spec.url.isEmpty();
}

void refreshModelTable(Application *app, QTableWidget *table) {
    ModelRegistry &registry = app->modelRegistry();
    const QList<ModelId> ids = allManagedModelIds();
    table->setRowCount(ids.size());
    for (int row = 0; row < ids.size(); ++row) {
        const ModelId id = ids.at(row);
        const ModelSpec spec = registry.spec(id);
        auto *name = new QTableWidgetItem(spec.displayName);
        name->setData(Qt::UserRole, static_cast<int>(id));
        table->setItem(row, 0, name);
        table->setItem(row, 1, new QTableWidgetItem(spec.purpose));
        table->setItem(row, 2, new QTableWidgetItem(formatSize(spec.size)));
        table->setItem(row, 3, new QTableWidgetItem(spec.estimatedRamLabel));
        table->setItem(row, 4, new QTableWidgetItem(spec.license));
        QString status;
        if (!isDownloadable(spec))
            status = QObject::tr("Not yet available");
        else
            status = registry.isAvailable(id) ? QObject::tr("Ready")
                                              : QObject::tr("Not downloaded");
        table->setItem(row, 5, new QTableWidgetItem(status));
        table->setItem(row, 6,
                       new QTableWidgetItem(ModelPolicy::isNeverDownload(app, id)
                                                ? QObject::tr("Never download")
                                                : QObject::tr("Ask first")));
    }
}

// Drives a registry-level download for a single ModelId with a
// cancellable progress dialog. Used from the manager UI.
bool downloadOneWithProgress(Application *app, QWidget *parent, ModelId id) {
    ModelRegistry &registry = app->modelRegistry();
    const ModelSpec spec = registry.spec(id);
    if (registry.isAvailable(id))
        return true;
    if (!isDownloadable(spec)) {
        QMessageBox::information(
            parent, QObject::tr("Model Not Available"),
            QObject::tr("%1 has no download source registered yet. "
                        "Install the file manually under the models directory.")
                .arg(spec.displayName));
        return false;
    }

    QProgressDialog progress(QObject::tr("Downloading %1…").arg(spec.displayName),
                             QObject::tr("Cancel"), 0, 100, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    bool ready = false;
    bool failed = false;
    QString failureMessage;

    auto progConn = QObject::connect(
        &registry, &ModelRegistry::downloadProgress, &progress,
        [&progress, id](ModelId gotId, qint64 rec, qint64 total) {
            if (gotId != id)
                return;
            if (total <= 0) {
                progress.setRange(0, 0);
                return;
            }
            progress.setRange(0, 100);
            progress.setValue(static_cast<int>(rec * 100 / total));
        });
    auto availConn = QObject::connect(&registry, &ModelRegistry::available, &progress,
                                      [&progress, id, &ready](ModelId gotId, const QString &) {
                                          if (gotId != id)
                                              return;
                                          ready = true;
                                          progress.setValue(progress.maximum());
                                          progress.close();
                                      });
    auto failConn = QObject::connect(
        &registry, &ModelRegistry::downloadFailed, &progress,
        [&progress, id, &failed, &failureMessage](ModelId gotId, const QString &message) {
            if (gotId != id)
                return;
            failed = true;
            failureMessage = message;
            progress.close();
        });

    registry.ensureAvailable(id);
    progress.exec();

    QObject::disconnect(progConn);
    QObject::disconnect(availConn);
    QObject::disconnect(failConn);

    if (failed) {
        QMessageBox::warning(
            parent, QObject::tr("Download Failed"),
            QObject::tr("Could not fetch %1:\n%2").arg(spec.displayName, failureMessage));
        return false;
    }
    return ready || registry.isAvailable(id);
}

// Builds the "Manage ML Models" dialog. Action buttons enable/disable
// based on the currently-selected row.
void buildAndExecManager(QWidget *parent, Application *app) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("ML Models"));
    dialog.resize(980, 420);

    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(QObject::tr(
        "ML features run locally on your device. Trailer does not "
        "send your image or PDF content to the cloud.\n"
        "Use this panel to review model size, estimated RAM, and "
        "download policy."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({QObject::tr("Model"), QObject::tr("Used for"),
                                      QObject::tr("Download size"), QObject::tr("Estimated RAM"),
                                      QObject::tr("License"), QObject::tr("Status"),
                                      QObject::tr("Policy")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int col = 2; col < 7; ++col)
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    refreshModelTable(app, table);
    table->selectRow(0);
    layout->addWidget(table);

    auto *rowButtons = new QHBoxLayout;
    auto *downloadNow = new QPushButton(QObject::tr("Download Selected"), &dialog);
    auto *setNever = new QPushButton(QObject::tr("Set Never Download"), &dialog);
    auto *allowAgain = new QPushButton(QObject::tr("Set Ask First"), &dialog);
    rowButtons->addWidget(downloadNow);
    rowButtons->addWidget(setNever);
    rowButtons->addWidget(allowAgain);
    rowButtons->addStretch();
    layout->addLayout(rowButtons);

    auto selectedId = [table]() -> std::optional<ModelId> {
        const int row = table->currentRow();
        if (row < 0)
            return std::nullopt;
        const auto *item = table->item(row, 0);
        if (!item)
            return std::nullopt;
        return static_cast<ModelId>(item->data(Qt::UserRole).toInt());
    };

    // Sync action-button enabled state to the current row: only one of
    // setNever/allowAgain is meaningful at a time, and downloadNow is
    // grey when the model is already cached, has no source, or is
    // policy-blocked.
    auto syncButtons = [&]() {
        const auto id = selectedId();
        if (!id) {
            downloadNow->setEnabled(false);
            setNever->setEnabled(false);
            allowAgain->setEnabled(false);
            return;
        }
        const ModelSpec spec = app->modelRegistry().spec(*id);
        const bool blocked = ModelPolicy::isNeverDownload(app, *id);
        downloadNow->setEnabled(isDownloadable(spec) &&
                                !app->modelRegistry().isAvailable(*id) && !blocked);
        setNever->setEnabled(!blocked);
        allowAgain->setEnabled(blocked);
    };
    syncButtons();
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, syncButtons);

    QObject::connect(downloadNow, &QPushButton::clicked, &dialog, [&]() {
        const auto id = selectedId();
        if (!id)
            return;
        downloadOneWithProgress(app, &dialog, *id);
        refreshModelTable(app, table);
        syncButtons();
    });
    QObject::connect(setNever, &QPushButton::clicked, &dialog, [&]() {
        const auto id = selectedId();
        if (!id)
            return;
        ModelPolicy::setNeverDownload(app, *id, true);
        refreshModelTable(app, table);
        syncButtons();
    });
    QObject::connect(allowAgain, &QPushButton::clicked, &dialog, [&]() {
        const auto id = selectedId();
        if (!id)
            return;
        ModelPolicy::setNeverDownload(app, *id, false);
        refreshModelTable(app, table);
        syncButtons();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

} // namespace

void showModelManagerDialog(QWidget *parent, Application *app) {
    buildAndExecManager(parent, app);
}

bool requestModelDownload(const ModelDownloadRequest &req) {
    if (req.isReady())
        return true;

    // Policy gate. Callers should already have disabled their menu
    // entry (see MainWindow::onCurrentDocumentChanged) so this branch
    // is a defensive guard against keyboard shortcuts or future call
    // paths that haven't picked up the same enable/disable logic. Fail
    // silently — the user has explicitly opted out of downloads.
    if (anyNeverDownloadEnabled(req.app, req.required))
        return false;

    QMessageBox box(req.parent);
    box.setWindowTitle(QObject::tr("Download %1 Model").arg(req.featureName));
    box.setIcon(QMessageBox::Question);
    box.setText(QObject::tr("%1 can download %2 for %3 (%4).")
                    .arg(req.featureName,
                         formatSize(totalSizeBytes(req.app->modelRegistry(), req.required)),
                         req.modelLabel, req.licenseLabel));
    box.setInformativeText(
        QObject::tr("Runs locally on your device (no cloud processing). "
                    "Choose Download Now to continue, or Manage ML Models for details."));
    auto *download = box.addButton(QObject::tr("Download Now"), QMessageBox::AcceptRole);
    auto *manage = box.addButton(QObject::tr("Manage ML Models…"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == manage) {
        // Open the manager — user might download from there. Return
        // ready state directly; do not fall through to a second prompt
        // or a redundant progress dialog.
        showModelManagerDialog(req.parent, req.app);
        return req.isReady();
    }
    if (box.clickedButton() != download)
        return false;

    QProgressDialog progress(req.progressMessage, QObject::tr("Cancel"), 0, 100, req.parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    bool ready = false;
    bool failed = false;
    QString failureMessage;
    req.wireSignals(&progress, &ready, &failed, &failureMessage);

    req.kickoff();
    progress.exec();

    if (progress.wasCanceled() && !ready)
        return false;
    if (failed) {
        QMessageBox::warning(req.parent, QObject::tr("Download Failed"),
                             QObject::tr("Could not fetch %1:\n%2")
                                 .arg(req.failureSubject, failureMessage));
        return false;
    }
    return req.isReady();
}

} // namespace trailer
