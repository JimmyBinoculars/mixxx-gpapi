#pragma once

#include <QDialog>
#include <QMap>

#include "plugins/pluginmanifest.h"
#include "plugins/pluginpermissions.h"

class QCheckBox;

namespace mixxx {
namespace plugins {

/// Modal consent dialog shown the first time a script plugin is loaded (or
/// when an update requests new capabilities). Native plugins are trusted and
/// do not use this dialog.
class PluginConsentDialog : public QDialog {
    Q_OBJECT
  public:
    PluginConsentDialog(const PluginManifest& manifest,
            PluginCapabilitySet granted,
            QWidget* pParent = nullptr);

    PluginCapabilitySet selectedCapabilities() const;

    /// Runs the dialog. On acceptance, writes the selection into `pGranted`
    /// and returns true. On rejection returns false.
    static bool askConsent(const PluginManifest& manifest,
            PluginCapabilitySet* pGranted,
            QWidget* pParent = nullptr);

  private:
    QMap<PluginCapability, QCheckBox*> m_checkBoxes;
};

} // namespace plugins
} // namespace mixxx
