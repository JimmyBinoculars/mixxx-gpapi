#pragma once

#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QSortFilterProxyModel>
#include <QStringList>

#include "control/controlsortfiltermodel.h"
#include "dialog/ui_dlgdevelopertoolsdlg.h"
#include "preferences/usersettings.h"
#include "util/statmodel.h"

namespace mixxx {
namespace plugins {
class PluginManager;
} // namespace plugins
} // namespace mixxx

class DlgDeveloperTools : public QDialog, public Ui::DlgDeveloperTools {
    Q_OBJECT
  public:
    DlgDeveloperTools(QWidget* pParent,
            UserSettingsPointer pConfig,
            mixxx::plugins::PluginManager* pPluginManager = nullptr);

  protected:
    void timerEvent(QTimerEvent* pTimerEvent) override;
    bool eventFilter(QObject* pObject, QEvent* pEvent) override;

  private slots:
    void slotControlSearch(const QString& search);
    void slotLogSearch();
    void slotControlDump();
    void slotConsoleRun();

  private:
    void appendConsole(const QString& text);

    UserSettingsPointer m_pConfig;
    ControlSortFilterModel m_controlProxyModel;

    StatModel m_statModel;
    QSortFilterProxyModel m_statProxyModel;

    QFile m_logFile;
    QTextCursor m_logCursor;

    mixxx::plugins::PluginManager* m_pPluginManager;
    QStringList m_consoleHistory;
    int m_consoleHistoryIndex;
};
