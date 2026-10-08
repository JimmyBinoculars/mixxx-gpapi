#include "plugins/pluginconsentdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "moc_pluginconsentdialog.cpp"

namespace mixxx {
namespace plugins {

PluginConsentDialog::PluginConsentDialog(const PluginManifest& manifest,
        PluginCapabilitySet granted,
        QWidget* pParent)
        : QDialog(pParent) {
    setWindowTitle(tr("Plugin Permissions"));
    setModal(true);

    auto* pLayout = new QVBoxLayout(this);

    auto* pTitle = new QLabel(this);
    pTitle->setTextFormat(Qt::RichText);
    pTitle->setText(tr("<b>%1</b> wants to use the following capabilities:")
                            .arg(manifest.name.isEmpty() ? manifest.id : manifest.name));
    pLayout->addWidget(pTitle);

    if (!manifest.unknownCapabilities.isEmpty()) {
        auto* pUnknown = new QLabel(this);
        pUnknown->setWordWrap(true);
        pUnknown->setText(tr("Note: the manifest requests unknown capabilities "
                             "that will be ignored: %1")
                                  .arg(manifest.unknownCapabilities.join(", ")));
        pLayout->addWidget(pUnknown);
    }

    static const PluginCapability kOrder[] = {
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

    for (PluginCapability capability : kOrder) {
        if (!manifest.requestedCapabilities.testFlag(capability)) {
            continue;
        }
        auto* pCheckBox = new QCheckBox(
                QStringLiteral("%1 — %2")
                        .arg(capabilityToString(capability),
                                capabilityDescription(capability)),
                this);
        pCheckBox->setChecked(granted.testFlag(capability));
        m_checkBoxes.insert(capability, pCheckBox);
        pLayout->addWidget(pCheckBox);
    }

    auto* pWarning = new QLabel(this);
    pWarning->setWordWrap(true);
    pWarning->setText(tr("Warning: the JavaScript runtime is not a security "
                         "sandbox. Only install plugins you trust."));
    pLayout->addWidget(pWarning);

    auto* pButtonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    pButtonBox->button(QDialogButtonBox::Ok)->setText(tr("Allow selected"));
    pLayout->addWidget(pButtonBox);

    connect(pButtonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(pButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PluginCapabilitySet PluginConsentDialog::selectedCapabilities() const {
    PluginCapabilitySet selected;
    for (auto it = m_checkBoxes.constBegin(); it != m_checkBoxes.constEnd(); ++it) {
        if (it.value()->isChecked()) {
            selected |= it.key();
        }
    }
    return selected;
}

bool PluginConsentDialog::askConsent(const PluginManifest& manifest,
        PluginCapabilitySet* pGranted,
        QWidget* pParent) {
    PluginCapabilitySet granted = pGranted != nullptr ? *pGranted : PluginCapabilitySet();
    if (manifest.requestedCapabilities == PluginCapabilitySet()) {
        return true;
    }

    PluginConsentDialog dialog(manifest, granted, pParent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (pGranted != nullptr) {
        *pGranted = dialog.selectedCapabilities();
    }
    return true;
}

} // namespace plugins
} // namespace mixxx
