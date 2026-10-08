#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <memory>

#include "plugins/pluginmanifest.h"
#include "plugins/pluginpermissions.h"
#include "plugins/library/remotesourcemanager.h"
#include "plugins/ui/pluginpanel.h"
#include "preferences/usersettings.h"

class QDockWidget;
class QLibrary;
class QMainWindow;
class QNetworkAccessManager;
class QJSEngine;
class QJSValue;

class Library;
class PlayerManager;
class TrackCollectionManager;

namespace mixxx {
namespace plugins {

class AudioGraph;
class PluginApi;
class PluginNode;
class PluginMenuRegistry;
class PluginStore;
class PluginScriptEngine;
struct PluginInstance;

/// Snapshot of a discovered plugin, suitable for display in the preferences
/// dialog.
struct PluginInfo {
    PluginManifest manifest;
    QString directory;
    bool valid = false;
    bool supported = false;
    QString error;
    bool enabled = true;
    bool loaded = false;
};

/// Dependencies the plugin subsystem needs from the rest of Mixxx. Filled in by
/// CoreServices after the corresponding services have been created.
struct PluginHost {
    UserSettingsPointer pConfig;
    std::shared_ptr<PlayerManager> pPlayerManager;
    std::shared_ptr<Library> pLibrary;
    std::shared_ptr<TrackCollectionManager> pTrackCollectionManager;
    PluginMenuRegistry* pMenuRegistry = nullptr;
    AudioGraph* pAudioGraph = nullptr;
};

/// Owns discovery, lifecycle and the JS/native runtimes of all plugins.
class PluginManager : public QObject {
    Q_OBJECT
  public:
    explicit PluginManager(PluginHost host, QObject* pParent = nullptr);
    ~PluginManager() override;

    PluginHost host() const {
        return m_host;
    }

    PluginPermissionStore* permissions() const {
        return m_pPermissions.get();
    }

    PluginMenuRegistry* menuRegistry() const {
        return m_host.pMenuRegistry;
    }

    AudioGraph* audioGraph() const {
        return m_host.pAudioGraph;
    }

    PluginStore* store() const {
        return m_pStore;
    }

    QNetworkAccessManager* networkManager() const {
        return m_pNetworkManager;
    }

    /// Remote music sources contributed by script plugins via `mixxx.remote`.
    RemoteSourceManager* remoteSources() const {
        return m_pRemoteSources;
    }

    /// Directory where user plugins live: ~/.mixxx/plugins
    QString userPluginPath() const;

    /// Directory where bundled example plugins live: <resourcePath>/plugins
    QString bundledPluginPath() const;

    /// Scans both plugin roots and updates the discovered list.
    void discover();

    /// Discovers and loads every enabled, supported plugin.
    void loadAll();

    /// Loads a single plugin by id (enabled or not).
    bool loadPlugin(const QString& pluginId);

    /// Unloads a plugin (shutting down its script engine / node).
    void unloadPlugin(const QString& pluginId);

    /// Enables/disables a plugin, loading or unloading it as needed.
    void setPluginEnabled(const QString& pluginId, bool enabled);

    /// Reloads a plugin (hot reload for scripts).
    void reloadPlugin(const QString& pluginId);

    /// Removes a plugin directory and its settings.
    bool uninstallPlugin(const QString& pluginId);

    QList<PluginInfo> plugins() const {
        return m_plugins;
    }

    /// Returns the info for a plugin id, or a default-constructed value.
    PluginInfo pluginInfo(const QString& pluginId) const;

    /// Sets the main window used to host plugin panels. Panels requested before
    /// this call are materialized when it is set.
    void setMainWindow(QMainWindow* pMainWindow);

    /// Adds a dockable panel for a plugin. The spec may render native
    /// declarative controls (works in every build) and/or a QML file (used
    /// when this build has QML support). Returns a panel id that can be passed
    /// to removePanel().
    QString addPanel(const PluginPanelSpec& spec);
    void removePanel(const QString& panelId);
    void removePluginPanels(const QString& pluginId);

    /// Updates a declarative control's value without invoking its callback.
    bool setPanelValue(const QString& panelId,
            const QString& controlId,
            const QVariant& value);

    /// Replaces the items of a declarative "list" control.
    bool setPanelItems(const QString& panelId,
            const QString& controlId,
            const QStringList& items);

    /// Replaces the items, values and optional icons of a "list" control.
    /// `icons` is parallel to `texts`; empty entries leave a row without an
    /// icon.
    bool setPanelListItems(const QString& panelId,
            const QString& controlId,
            const QStringList& texts,
            const QVariantList& values,
            const QStringList& icons);

    /// Sets (or clears, with an empty path) the icon of a single "list" row
    /// without disturbing the rest of the list or its selection.
    bool setPanelListItemIcon(const QString& panelId,
            const QString& controlId,
            int index,
            const QString& iconPath);

    /// Enables/disables a declarative control.
    bool setPanelEnabled(const QString& panelId,
            const QString& controlId,
            bool enabled);

    /// Shows (and raises) or hides a panel. Returns false for panels that are
    /// still pending (the main window does not exist yet).
    bool setPanelVisible(const QString& panelId, bool visible);
    bool isPanelVisible(const QString& panelId) const;

    /// True if the panel is a dialog window (as opposed to a dock).
    bool isPanelDialog(const QString& panelId) const;

    /// Shows the panel fullscreen (true) or restores it (false). Returns false
    /// for pending panels.
    bool setPanelFullscreen(const QString& panelId, bool fullscreen);
    bool isPanelFullscreen(const QString& panelId) const;
    /// Toggles fullscreen and returns the resulting state.
    bool togglePanelFullscreen(const QString& panelId);

    /// Keeps the panel above other windows. Returns false for pending panels.
    bool setPanelAlwaysOnTop(const QString& panelId, bool onTop);

    /// Evaluates JavaScript in a web panel's page. Returns false when the panel
    /// is not a web panel or this build has no WebEngine support.
    bool evaluatePanelWeb(const QString& panelId, const QString& code);

    /// Moves/resizes a panel's window. Pass a negative x/y to keep the current
    /// position. Returns false for pending panels.
    bool setPanelGeometry(const QString& panelId,
            int x,
            int y,
            int width,
            int height);

    /// Current window geometry as { x, y, width, height }; empty if unknown.
    QVariantMap panelGeometry(const QString& panelId) const;

    /// Loads a native plugin shared library and inserts it into the audio
    /// graph as a standalone node. Returns the graph node id, or an empty
    /// string on failure. `pluginId` is used for the auto-generated parameter
    /// panel.
    QString addNativeGraphNode(const QString& pluginId,
            const QString& libraryPath, QString* pError);

    /// Removes a node previously added by addNativeGraphNode().
    bool removeGraphNode(const QString& nodeId);

    /// A QJSEngine wired to the full plugin API, used by the developer REPL.
    /// The returned engine is owned by the manager.
    QJSEngine* replEngine();

    /// Evaluates a snippet in the REPL engine. Returns true and fills
    /// `pResult` on success.
    bool evaluateRepl(const QString& code, QJSValue* pResult);

  protected:
    bool eventFilter(QObject* pObject, QEvent* pEvent) override;

  signals:
    void pluginsChanged();
    void pluginLoaded(const QString& pluginId);
    void pluginUnloaded(const QString& pluginId);
    void pluginLoadFailed(const QString& pluginId, const QString& error);

  private:
    bool loadScriptPlugin(PluginInstance& instance, const PluginInfo& info);
    bool loadNativePlugin(PluginInstance& instance, const PluginInfo& info);
    PluginInfo* findPlugin(const QString& pluginId);
    const PluginInfo* findPlugin(const QString& pluginId) const;
    void scanDirectory(const QString& root, QList<PluginInfo>* pOut) const;
    void rebuildNativeChain();
    void ensureReplEngine();
    void materializePanel(const PluginPanelSpec& spec, const QString& panelId);

    /// Builds the content widget for a panel (QML when available, otherwise the
    /// native declarative controls). Registers the widget for setPanelValue().
    QWidget* buildPanelContent(
            const PluginPanelSpec& spec, QWidget* pParent, const QString& panelId);

    /// Builds a native-contributed parameter panel from the plugin's
    /// describe_param()/num_params() callbacks and wires it to its node.
    QString addNativeParamPanel(
            const QString& pluginId, const std::shared_ptr<PluginNode>& pNode);

    PluginHost m_host;
    std::unique_ptr<PluginPermissionStore> m_pPermissions;
    PluginStore* m_pStore;
    QNetworkAccessManager* m_pNetworkManager;
    RemoteSourceManager* m_pRemoteSources = nullptr;
    /// Nulls itself out if the main window is destroyed (it outlives us when
    /// the application tears down in main()).
    QPointer<QMainWindow> m_pMainWindow;

    QList<PluginInfo> m_plugins;
    QHash<QString, PluginInstance*> m_instances;

    /// Native graph nodes in load order, used to build the default chain.
    QStringList m_nativeChain;

    struct PendingPanel {
        PluginPanelSpec spec;
        QString panelId;
    };
    QList<PendingPanel> m_pendingPanels;
    /// panel id -> container (QDockWidget* or PluginPanelWindow*).
    QHash<QString, QPointer<QWidget>> m_panels;
    QHash<QString, QPointer<PluginPanelWidget>> m_panelWidgets;
    /// panel id -> embedded web view (web panels only).
    QHash<QString, QPointer<QWidget>> m_webViews;
    QHash<QString, QString> m_panelPluginIds;
    /// panel container -> onClose callback (fires when the user closes it).
    QHash<QObject*, std::function<void()>> m_panelCloseCallbacks;
    /// Window geometry saved before emulated fullscreen (WM fallback).
    QHash<QString, QRect> m_panelNormalGeometry;
    /// A node created through addNativeGraphNode(), keyed by graph node id.
    /// Records the owning plugin so its nodes can be torn down when it
    /// unloads, plus the library path so the backing QLibrary can be released
    /// together with the node.
    struct NativeGraphNode {
        QString pluginId;
        QString libraryPath;
    };
    QHash<QString, NativeGraphNode> m_graphNodes;
    QHash<QString, QString> m_paramPanels; // pluginId -> param panel id

    QJSEngine* m_pReplEngine = nullptr;
    /// The REPL's PluginApi. Deleted before m_pReplEngine and m_pRemoteSources
    /// in the destructor so its own destructor never touches freed state.
    PluginApi* m_pReplApi = nullptr;

    friend class PluginApi;
};

} // namespace plugins
} // namespace mixxx
