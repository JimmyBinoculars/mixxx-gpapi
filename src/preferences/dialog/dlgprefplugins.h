#pragma once

#include <QMap>

#include "dialog/ui_dlgprefpluginsdlg.h"
#include "plugins/pluginpermissions.h"
#include "preferences/dialog/dlgpreferencepage.h"

class QCheckBox;

namespace mixxx {
namespace plugins {
class PluginManager;
}
} // namespace mixxx

class DlgPrefPlugins : public DlgPreferencePage, public Ui::DlgPrefPlugins {
    Q_OBJECT
  public:
    DlgPrefPlugins(QWidget* pParent, mixxx::plugins::PluginManager* pManager);
    ~DlgPrefPlugins() override;

    QUrl helpUrl() const override;

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private slots:
    void slotSelectionChanged();
    void slotToggleEnabled();
    void slotReload();
    void slotUninstall();
    void slotOpenFolder();
    void slotInstallFromFile();
    void slotInstallFromUrl();
    void slotPluginsChanged();

  private:
    void refreshTable();
    void refreshDetails();
    QString selectedPluginId() const;

    mixxx::plugins::PluginManager* m_pManager;
    QMap<mixxx::plugins::PluginCapability, QCheckBox*> m_permissionBoxes;
    QString m_selectedPluginId;
};
