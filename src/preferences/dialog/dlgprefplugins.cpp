#include "preferences/dialog/dlgprefplugins.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLayoutItem>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QUrl>

#include "moc_dlgprefplugins.cpp"
#include "plugins/pluginmanager.h"
#include "plugins/pluginstore.h"
#include "util/desktophelper.h"
#include "util/versionstore.h"

using namespace mixxx::plugins;

namespace {

const PluginCapability kCapabilityOrder[] = {
        PluginCapability::ControlsRead,
        PluginCapability::ControlsWrite,
        PluginCapability::UiMenu,
        PluginCapability::UiPanel,
        PluginCapability::Network,
        PluginCapability::FilesRead,
        PluginCapability::FilesWrite,
        PluginCapability::LibraryRead,
        PluginCapability::LibraryWrite,
        PluginCapability::AudioGraph,
        PluginCapability::Download,
        PluginCapability::Exec,
        PluginCapability::FilesPaths,
        PluginCapability::UiWeb,
        PluginCapability::AudioScope,
        PluginCapability::LibraryRemote,
};

} // anonymous namespace

DlgPrefPlugins::DlgPrefPlugins(QWidget* pParent, PluginManager* pManager)
        : DlgPreferencePage(pParent),
          m_pManager(pManager) {
    setupUi(this);

    apiVersionLabel->setText(tr("Plugin API version %1 (minimum supported %2).")
                    .arg(VersionStore::pluginApiVersion())
                    .arg(VersionStore::pluginApiMinVersion()));

    pluginsTable->setColumnCount(4);
    pluginsTable->setHorizontalHeaderLabels(
            {tr("Name"), tr("Version"), tr("Runtime"), tr("Status")});
    pluginsTable->verticalHeader()->setVisible(false);
    pluginsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pluginsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    pluginsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(pluginsTable,
            &QTableWidget::itemSelectionChanged,
            this,
            &DlgPrefPlugins::slotSelectionChanged);
    connect(enableButton, &QPushButton::clicked, this, &DlgPrefPlugins::slotToggleEnabled);
    connect(reloadButton, &QPushButton::clicked, this, &DlgPrefPlugins::slotReload);
    connect(uninstallButton, &QPushButton::clicked, this, &DlgPrefPlugins::slotUninstall);
    connect(openFolderButton, &QPushButton::clicked, this, &DlgPrefPlugins::slotOpenFolder);
    connect(installFileButton, &QPushButton::clicked, this, &DlgPrefPlugins::slotInstallFromFile);
    connect(installUrlButton, &QPushButton::clicked, this, &DlgPrefPlugins::slotInstallFromUrl);
    connect(m_pManager,
            &PluginManager::pluginsChanged,
            this,
            &DlgPrefPlugins::slotPluginsChanged);

    refreshTable();
}

DlgPrefPlugins::~DlgPrefPlugins() = default;

QUrl DlgPrefPlugins::helpUrl() const {
    return QUrl(QStringLiteral("https://manual.mixxx.org/latest/en/chapters/plugins.html"));
}

void DlgPrefPlugins::slotUpdate() {
    m_pManager->discover();
    refreshTable();
}

void DlgPrefPlugins::slotApply() {
    if (m_selectedPluginId.isEmpty()) {
        return;
    }
    PluginCapabilitySet selected;
    PluginCapabilitySet shown;
    for (auto it = m_permissionBoxes.constBegin(); it != m_permissionBoxes.constEnd(); ++it) {
        shown |= it.key();
        if (it.value()->isChecked()) {
            selected |= it.key();
        }
    }
    const PluginCapabilitySet previous =
            m_pManager->permissions()->grantedCapabilities(m_selectedPluginId);
    // Never revoke a granted capability that has no checkbox on this page (for
    // example one that is not in the display order): applying the preferences
    // must not silently drop permissions.
    selected |= previous & ~shown;
    m_pManager->permissions()->setGrantedCapabilities(m_selectedPluginId, selected);
    if (previous != selected && m_pManager->pluginInfo(m_selectedPluginId).loaded) {
        m_pManager->reloadPlugin(m_selectedPluginId);
    }
}

void DlgPrefPlugins::slotResetToDefaults() {
    if (m_selectedPluginId.isEmpty()) {
        return;
    }
    // Reset only the checkboxes; like every other preferences page the change
    // is persisted on Apply, so Cancel leaves the stored grants untouched.
    const PluginInfo info = m_pManager->pluginInfo(m_selectedPluginId);
    const PluginCapabilitySet requested = info.manifest.requestedCapabilities;
    for (auto it = m_permissionBoxes.constBegin(); it != m_permissionBoxes.constEnd(); ++it) {
        it.value()->setChecked(requested.testFlag(it.key()));
    }
}

QString DlgPrefPlugins::selectedPluginId() const {
    const QList<QTableWidgetItem*> items = pluginsTable->selectedItems();
    if (items.isEmpty()) {
        return QString();
    }
    const QTableWidgetItem* pItem = pluginsTable->item(items.first()->row(), 0);
    return pItem != nullptr ? pItem->data(Qt::UserRole).toString() : QString();
}

void DlgPrefPlugins::refreshTable() {
    const QString previousSelection = m_selectedPluginId;
    pluginsTable->setRowCount(0);

    const QList<PluginInfo> plugins = m_pManager->plugins();
    for (const PluginInfo& info : plugins) {
        const int row = pluginsTable->rowCount();
        pluginsTable->insertRow(row);

        const QString name = info.manifest.name.isEmpty()
                ? info.manifest.id
                : info.manifest.name;

        auto* pNameItem = new QTableWidgetItem(name);
        pNameItem->setData(Qt::UserRole, info.manifest.id);
        if (!info.error.isEmpty()) {
            pNameItem->setToolTip(info.error);
        }
        pluginsTable->setItem(row, 0, pNameItem);

        pluginsTable->setItem(row, 1, new QTableWidgetItem(info.manifest.version));

        QString runtime = info.manifest.runtimeString();
        if (!info.valid) {
            runtime = QStringLiteral("?");
        }
        pluginsTable->setItem(row, 2, new QTableWidgetItem(runtime));

        QString status;
        if (!info.valid) {
            status = tr("Invalid manifest");
        } else if (!info.supported) {
            status = tr("Unsupported");
        } else if (!info.enabled) {
            status = tr("Disabled");
        } else if (info.loaded) {
            status = tr("Loaded");
        } else {
            status = tr("Enabled (not loaded)");
        }
        auto* pStatusItem = new QTableWidgetItem(status);
        if (!info.error.isEmpty()) {
            pStatusItem->setToolTip(info.error);
        }
        pluginsTable->setItem(row, 3, pStatusItem);

        if (info.manifest.id == previousSelection) {
            pluginsTable->selectRow(row);
        }
    }
    refreshDetails();
}

void DlgPrefPlugins::refreshDetails() {
    m_selectedPluginId = selectedPluginId();

    // Clear old capability checkboxes.
    m_permissionBoxes.clear();
    QLayoutItem* pChild = nullptr;
    while ((pChild = permissionsLayout->takeAt(0)) != nullptr) {
        if (pChild->widget() != nullptr) {
            pChild->widget()->deleteLater();
        }
        delete pChild;
    }

    if (m_selectedPluginId.isEmpty()) {
        detailsLabel->setText(tr("Select a plugin to see its details."));
        permissionsGroup->setEnabled(false);
        enableButton->setEnabled(false);
        reloadButton->setEnabled(false);
        uninstallButton->setEnabled(false);
        return;
    }

    permissionsGroup->setEnabled(true);
    enableButton->setEnabled(true);
    reloadButton->setEnabled(true);
    uninstallButton->setEnabled(true);

    const PluginInfo info = m_pManager->pluginInfo(m_selectedPluginId);
    // Make the toggle a clear verb instead of the ambiguous
    // "Enable / Disable" so it is obvious what a click will do.
    enableButton->setText(info.enabled ? tr("Disable") : tr("Enable"));
    QString details = QStringLiteral("<b>%1</b> <i>%2</i><br/>%3")
                              .arg(info.manifest.id,
                                      info.manifest.runtimeString(),
                                      info.manifest.description.toHtmlEscaped());
    if (!info.error.isEmpty()) {
        details += QStringLiteral("<br/><span style='color:#c00;'>%1</span>")
                           .arg(info.error.toHtmlEscaped());
    }
    detailsLabel->setText(details);

    const PluginCapabilitySet requested = info.manifest.requestedCapabilities;
    const PluginCapabilitySet granted =
            m_pManager->permissions()->grantedCapabilities(m_selectedPluginId);
    const bool editable = info.valid && info.supported &&
            info.manifest.runtime == PluginManifest::Runtime::Script;

    auto addPermissionBox = [&](PluginCapability capability) {
        if (m_permissionBoxes.contains(capability)) {
            return;
        }
        auto* pCheckBox = new QCheckBox(
                QStringLiteral("%1 — %2")
                        .arg(capabilityToString(capability),
                                capabilityDescription(capability)),
                permissionsGroup);
        pCheckBox->setChecked(granted.testFlag(capability));
        pCheckBox->setEnabled(editable);
        permissionsLayout->addWidget(pCheckBox);
        m_permissionBoxes.insert(capability, pCheckBox);
    };

    for (PluginCapability capability : kCapabilityOrder) {
        if (requested.testFlag(capability)) {
            addPermissionBox(capability);
        }
    }
    // Defensive: surface any requested capability that is not in the display
    // order, so it can neither be hidden nor silently revoked on Apply.
    for (int bit = 0; bit < 32; ++bit) {
        const auto capability = static_cast<PluginCapability>(1u << bit);
        if (requested.testFlag(capability) &&
                !m_permissionBoxes.contains(capability)) {
            addPermissionBox(capability);
        }
    }
    if (m_permissionBoxes.isEmpty()) {
        auto* pLabel = new QLabel(tr("This plugin requests no capabilities."), permissionsGroup);
        permissionsLayout->addWidget(pLabel);
    }
}

void DlgPrefPlugins::slotSelectionChanged() {
    refreshDetails();
}

void DlgPrefPlugins::slotToggleEnabled() {
    if (m_selectedPluginId.isEmpty()) {
        return;
    }
    const PluginInfo info = m_pManager->pluginInfo(m_selectedPluginId);
    m_pManager->setPluginEnabled(m_selectedPluginId, !info.enabled);
}

void DlgPrefPlugins::slotReload() {
    if (!m_selectedPluginId.isEmpty()) {
        m_pManager->reloadPlugin(m_selectedPluginId);
    }
}

void DlgPrefPlugins::slotUninstall() {
    if (m_selectedPluginId.isEmpty()) {
        return;
    }
    const PluginInfo info = m_pManager->pluginInfo(m_selectedPluginId);
    if (info.directory == m_pManager->bundledPluginPath() ||
            info.directory.startsWith(m_pManager->bundledPluginPath() + QDir::separator())) {
        QMessageBox::information(this,
                tr("Uninstall Plugin"),
                tr("Bundled plugins cannot be uninstalled."));
        return;
    }
    if (QMessageBox::question(this,
                tr("Uninstall Plugin"),
                tr("Remove plugin '%1' and all of its files?")
                        .arg(info.manifest.name.isEmpty() ? info.manifest.id
                                                          : info.manifest.name)) !=
            QMessageBox::Yes) {
        return;
    }
    if (!m_pManager->uninstallPlugin(m_selectedPluginId)) {
        QMessageBox::warning(this,
                tr("Uninstall Plugin"),
                tr("Failed to remove the plugin."));
    }
    m_selectedPluginId.clear();
    refreshTable();
}

void DlgPrefPlugins::slotOpenFolder() {
    mixxx::DesktopHelper::openUrl(
            QUrl::fromLocalFile(m_pManager->userPluginPath()));
}

void DlgPrefPlugins::slotInstallFromFile() {
    const QString path = QFileDialog::getOpenFileName(this,
            tr("Install Plugin"),
            QString(),
            tr("Plugin archives (*.zip *.tar.gz *.tgz)"));
    if (path.isEmpty()) {
        return;
    }
    QString pluginId;
    QString error;
    if (!m_pManager->store()->installFromArchive(path, &pluginId, &error)) {
        QMessageBox::warning(this, tr("Install Plugin"), error);
        return;
    }
    m_pManager->discover();
    QMessageBox::information(this,
            tr("Install Plugin"),
            tr("Installed plugin '%1'.").arg(pluginId));
}

void DlgPrefPlugins::slotInstallFromUrl() {
    const QString url = urlEdit->text().trimmed();
    if (url.isEmpty()) {
        return;
    }
    const QString sha = sha256Edit->text().trimmed();
    installUrlButton->setEnabled(false);
    // The install completes asynchronously and the preferences dialog may be
    // destroyed before the callback fires, so guard the captured `this`.
    QPointer<DlgPrefPlugins> pSelf(this);
    m_pManager->store()->installFromUrl(QUrl(url), sha,
            [pSelf](bool success, const QString& pluginId, const QString& error) {
                if (pSelf == nullptr) {
                    return;
                }
                pSelf->installUrlButton->setEnabled(true);
                if (!success) {
                    QMessageBox::warning(pSelf, tr("Install Plugin"), error);
                    return;
                }
                pSelf->m_pManager->discover();
                QMessageBox::information(pSelf,
                        tr("Install Plugin"),
                        tr("Installed plugin '%1'.").arg(pluginId));
            });
}

void DlgPrefPlugins::slotPluginsChanged() {
    refreshTable();
}
