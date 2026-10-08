#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "plugins/pluginpermissions.h"

class QJSEngine;
class QNetworkReply;

namespace mixxx {
namespace plugins {

class PluginManager;
class ControlsProxy;
class MenuProxy;
class UiProxy;
class NetProxy;
class FilesProxy;
class LibraryProxy;
class SettingsProxy;
class AudioProxy;
class DownloadProxy;
class ProcessProxy;
class CryptoProxy;
class RemoteProxy;

/// Aggregates the per-namespace JS proxy objects and performs capability
/// checks. Exposed to scripts as the global `mixxx` object.
class PluginApi : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* controls READ controls CONSTANT)
    Q_PROPERTY(QObject* menu READ menu CONSTANT)
    Q_PROPERTY(QObject* ui READ ui CONSTANT)
    Q_PROPERTY(QObject* net READ net CONSTANT)
    Q_PROPERTY(QObject* files READ files CONSTANT)
    Q_PROPERTY(QObject* library READ library CONSTANT)
    Q_PROPERTY(QObject* settings READ settings CONSTANT)
    Q_PROPERTY(QObject* audio READ audio CONSTANT)
    Q_PROPERTY(QObject* download READ download CONSTANT)
    Q_PROPERTY(QObject* process READ process CONSTANT)
    Q_PROPERTY(QObject* crypto READ crypto CONSTANT)
    Q_PROPERTY(QObject* remote READ remote CONSTANT)
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString pluginDir READ pluginDir CONSTANT)
    Q_PROPERTY(QString mixxxVersion READ mixxxVersion CONSTANT)

  public:
    PluginApi(QString pluginId,
            QString pluginDir,
            PluginCapabilitySet requestedCapabilities,
            PluginManager* pManager,
            QObject* pParent = nullptr);
    ~PluginApi() override;

    QObject* controls();
    QObject* menu();
    QObject* ui();
    QObject* net();
    QObject* files();
    QObject* library();
    QObject* settings();
    QObject* audio();
    QObject* download();
    QObject* process();
    QObject* crypto();
    QObject* remote();

    QString pluginId() const {
        return m_pluginId;
    }
    QString pluginDir() const {
        return m_pluginDir;
    }
    QString mixxxVersion() const;

    PluginManager* manager() const {
        return m_pManager;
    }

    QJSEngine* engine() const {
        return m_pEngine;
    }
    void setEngine(QJSEngine* pEngine) {
        m_pEngine = pEngine;
    }

    /// Bypasses capability gating. Used only by the developer REPL.
    void setBypassPermissionChecks(bool bypass) {
        m_bypassPermissionChecks = bypass;
    }

    /// True if the capability was requested by the manifest and granted by the
    /// user. Logs a warning (once per capability) otherwise.
    bool checkCapability(PluginCapability capability);

    Q_INVOKABLE void log(const QString& message);
    Q_INVOKABLE bool hasCapability(const QString& capability) const;

  private:
    QString m_pluginId;
    QString m_pluginDir;
    PluginCapabilitySet m_requestedCapabilities;
    PluginCapabilitySet m_deniedLogged;
    PluginManager* m_pManager;
    QJSEngine* m_pEngine = nullptr;
    bool m_bypassPermissionChecks = false;

    ControlsProxy* m_pControls;
    MenuProxy* m_pMenu;
    UiProxy* m_pUi;
    NetProxy* m_pNet;
    FilesProxy* m_pFiles;
    LibraryProxy* m_pLibrary;
    SettingsProxy* m_pSettings;
    AudioProxy* m_pAudio;
    DownloadProxy* m_pDownload;
    ProcessProxy* m_pProcess;
    CryptoProxy* m_pCrypto;
    RemoteProxy* m_pRemote;
};

/// mixxx.controls — read/write ControlObjects.
class ControlsProxy : public QObject {
    Q_OBJECT
  public:
    explicit ControlsProxy(PluginApi* pApi);

    Q_INVOKABLE double get(const QString& group, const QString& item) const;
    Q_INVOKABLE bool set(const QString& group, const QString& item, double value);
    Q_INVOKABLE bool trigger(const QString& group, const QString& item);
    Q_INVOKABLE bool exists(const QString& group, const QString& item) const;

  private:
    PluginApi* m_pApi;
};

/// mixxx.menu — add/remove main menu entries.
class MenuProxy : public QObject {
    Q_OBJECT
  public:
    explicit MenuProxy(PluginApi* pApi);

    /// options: { menu, submenu, text, shortcut, checkable, checked, callback }
    Q_INVOKABLE QString addItem(const QJSValue& options);
    Q_INVOKABLE void removeItem(const QString& itemId);
    Q_INVOKABLE void clear();

  private:
    PluginApi* m_pApi;
};

/// mixxx.ui — panels, notifications and dialogs.
class UiProxy : public QObject {
    Q_OBJECT
  public:
    explicit UiProxy(PluginApi* pApi);

    /// options: { title, area: "left"|"right"|"top"|"bottom", qml,
    ///            controls: [{ id, type, text, value, min, max, step,
    ///                         decimals, checked, choices, callback }],
    ///            onChange(id, value) }
    ///
    /// `controls` are rendered natively (no QML required). `qml` is only used
    /// when this build has QML support.
    Q_INVOKABLE QString addPanel(const QJSValue& options);
    /// Convenience wrapper around addPanel() that forces dialog mode.
    Q_INVOKABLE QString addDialog(const QJSValue& options);
    Q_INVOKABLE void removePanel(const QString& panelId);
    /// Updates a declarative control without invoking its callback.
    Q_INVOKABLE bool setPanelValue(
            const QString& panelId, const QString& controlId, const QJSValue& value);
    /// Replaces the items of a "list" control.
    Q_INVOKABLE bool setPanelItems(const QString& panelId,
            const QString& controlId,
            const QStringList& items);
    /// Replaces the items/values of a "list" control from a JS array of
    /// strings or { text, value, icon } objects.
    Q_INVOKABLE bool setPanelList(const QString& panelId,
            const QString& controlId,
            const QJSValue& items);
    /// Sets (or clears, with "") the icon of a single "list" row without
    /// disturbing the rest of the list or its selection. `index` is 0-based.
    Q_INVOKABLE bool setPanelListItemIcon(const QString& panelId,
            const QString& controlId,
            int index,
            const QString& iconPath);
    /// Enables/disables a control.
    Q_INVOKABLE bool setPanelEnabled(const QString& panelId,
            const QString& controlId,
            bool enabled);
    /// Shows (and raises) a panel. Returns false while the panel is pending.
    Q_INVOKABLE bool showPanel(const QString& panelId);
    /// Hides a panel.
    Q_INVOKABLE bool hidePanel(const QString& panelId);
    /// True if the panel exists and is visible.
    Q_INVOKABLE bool panelVisible(const QString& panelId) const;
    /// Moves/resizes a panel window. A negative x/y keeps the current position.
    Q_INVOKABLE bool setPanelGeometry(const QString& panelId,
            int x,
            int y,
            int width,
            int height);
    /// Current panel window geometry as { x, y, width, height }.
    Q_INVOKABLE QVariantMap panelGeometry(const QString& panelId) const;
    /// True if the panel was created as a dialog window rather than a dock.
    Q_INVOKABLE bool panelIsDialog(const QString& panelId) const;
    /// Shows the panel fullscreen or restores it.
    Q_INVOKABLE bool setPanelFullscreen(const QString& panelId, bool fullscreen);
    /// Toggles fullscreen; returns the resulting state.
    Q_INVOKABLE bool togglePanelFullscreen(const QString& panelId);
    Q_INVOKABLE bool panelFullscreen(const QString& panelId) const;
    /// Keeps the panel above other windows.
    Q_INVOKABLE bool setPanelAlwaysOnTop(const QString& panelId, bool onTop);
    /// Evaluates JavaScript inside a web panel's page. Requires `ui.web`.
    Q_INVOKABLE bool webEval(const QString& panelId, const QString& code);
    Q_INVOKABLE void notify(const QString& title, const QString& message);
    /// Native directory chooser. Returns "" if cancelled. Requires
    /// `files.paths`; the chosen folder is approved automatically.
    Q_INVOKABLE QString pickFolder(const QString& title, const QString& startDir);
    /// Native file chooser; approves the file's folder. Requires `files.paths`.
    Q_INVOKABLE QString pickFile(
            const QString& title, const QString& startDir, const QString& filter);
    /// Opens a path in the system file manager. Requires `ui.panel`.
    Q_INVOKABLE bool openInFileManager(const QString& path);
    /// Copies text to the clipboard. Requires `ui.panel`.
    Q_INVOKABLE void copyToClipboard(const QString& text);
    /// Returns the clipboard text. Requires `ui.panel`.
    Q_INVOKABLE QString clipboardText() const;

  private:
    QString addPanelImpl(const QJSValue& options, bool dialog);
    PluginApi* m_pApi;
};

/// mixxx.net — HTTP requests.
class NetProxy : public QObject {
    Q_OBJECT
  public:
    explicit NetProxy(PluginApi* pApi);

    /// callback(result) where result is { ok, status, error, body }.
    Q_INVOKABLE void get(const QString& url, const QJSValue& callback);
    Q_INVOKABLE void post(
            const QString& url, const QString& body, const QJSValue& callback);
    /// Downloads to a file inside the plugin directory. callback(result)
    /// where result is { ok, path, error }.
    Q_INVOKABLE void download(
            const QString& url, const QString& relativePath, const QJSValue& callback);
    /// Like download(), but also reports progress: onProgress(received, total).
    Q_INVOKABLE void downloadWithProgress(const QString& url,
            const QString& relativePath,
            const QJSValue& callback,
            const QJSValue& onProgress);
    /// Downloads to an absolute path that has been approved via `files.paths`.
    Q_INVOKABLE void downloadTo(const QString& url,
            const QString& absolutePath,
            const QJSValue& callback);
    /// Like downloadTo(), but also reports progress.
    Q_INVOKABLE void downloadToWithProgress(const QString& url,
            const QString& absolutePath,
            const QJSValue& callback,
            const QJSValue& onProgress);
    /// When enabled, TLS certificate errors (e.g. self-signed certificates) are
    /// ignored for subsequent requests. Requires `network`.
    Q_INVOKABLE void setIgnoreSslErrors(bool ignore);

  private:
    void handleReply(QNetworkReply* pReply, const QJSValue& callback);
    void downloadImpl(const QString& url,
            const QString& relativePath,
            const QJSValue& callback,
            const QJSValue& onProgress);
    void downloadToImpl(const QString& url,
            const QString& absolutePath,
            const QJSValue& callback,
            const QJSValue& onProgress);
    void startDownload(const QString& url,
            const QString& target,
            const QJSValue& callback,
            const QJSValue& onProgress);
    /// Connects the reply to ignoreSslErrors() when the plugin opted in.
    void attachIgnoreSsl(QNetworkReply* pReply);
    /// Returns the cleaned absolute path when `path` is inside an approved root,
    /// or an empty string otherwise.
    QString resolveApprovedTarget(const QString& path) const;
    PluginApi* m_pApi;
    bool m_ignoreSslErrors = false;
    QHash<QNetworkReply*, QJSValue> m_callbacks;
    QHash<QNetworkReply*, QJSValue> m_progressCallbacks;
};

/// mixxx.files — sandboxed file access inside the plugin directory, plus
/// explicitly approved paths outside it (`files.paths`).
class FilesProxy : public QObject {
    Q_OBJECT
  public:
    explicit FilesProxy(PluginApi* pApi);

    Q_INVOKABLE QString readText(const QString& relativePath) const;
    Q_INVOKABLE bool writeText(const QString& relativePath, const QString& contents);
    /// Deletes a file (inside the plugin folder or an approved path).
    /// Requires `files.write`.
    Q_INVOKABLE bool remove(const QString& path) const;
    Q_INVOKABLE bool exists(const QString& relativePath) const;
    Q_INVOKABLE QVariantList list(const QString& relativeDir) const;
    Q_INVOKABLE QString absolutePath(const QString& relativePath) const;

    /// Approves a path (and everything under it) for this plugin. Requires
    /// `files.paths`. Typically the path returned by a folder picker.
    Q_INVOKABLE bool allowPath(const QString& path);
    /// The currently approved paths.
    Q_INVOKABLE QStringList allowedPaths() const;
    /// Removes a previously approved path.
    Q_INVOKABLE bool revokePath(const QString& path);

  private:
    /// Returns the cleaned absolute path when `relativePath` is inside the
    /// plugin directory or an approved path, or an empty string otherwise.
    QString resolve(const QString& relativePath) const;
    PluginApi* m_pApi;
};

/// mixxx.library — track library access.
class LibraryProxy : public QObject {
    Q_OBJECT
  public:
    explicit LibraryProxy(PluginApi* pApi);

    Q_INVOKABLE int trackCount() const;
    Q_INVOKABLE QVariantMap getTrack(const QString& location) const;
    Q_INVOKABLE bool loadTrack(const QString& location, int deck, bool play);
    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void refresh();
    /// Scans the library folders and indexes new/changed files. Requires
    /// `library.read`.
    Q_INVOKABLE void rescan();
    /// Adds a folder to the library and triggers a scan. Requires
    /// `library.write`.
    Q_INVOKABLE bool addFolder(const QString& path);
    /// The directories the library currently watches. Requires `library.read`.
    Q_INVOKABLE QStringList folders() const;

  private:
    PluginApi* m_pApi;
};

/// mixxx.settings — per-plugin key/value settings.
class SettingsProxy : public QObject {
    Q_OBJECT
  public:
    explicit SettingsProxy(PluginApi* pApi);

    Q_INVOKABLE QVariant get(const QString& key, const QVariant& defaultValue) const;
    Q_INVOKABLE void set(const QString& key, const QVariant& value);
    Q_INVOKABLE void remove(const QString& key);

  private:
    QString settingKey(const QString& key) const;
    PluginApi* m_pApi;
};

/// mixxx.audio — real-time audio graph manipulation.
class AudioProxy : public QObject {
    Q_OBJECT
  public:
    explicit AudioProxy(PluginApi* pApi);

    Q_INVOKABLE QString addNativeNode(const QString& libraryPath);
    Q_INVOKABLE bool removeNode(const QString& nodeId);
    Q_INVOKABLE QVariantList nodes() const;
    Q_INVOKABLE bool connect(const QString& sourceNodeId,
            int sourceChannel,
            const QString& destNodeId,
            int destChannel);
    Q_INVOKABLE bool connectInput(int inputChannel,
            const QString& destNodeId,
            int destChannel);
    Q_INVOKABLE bool connectOutput(
            const QString& sourceNodeId, int sourceChannel, int outputChannel);
    Q_INVOKABLE bool disconnect(const QString& destNodeId, int destChannel);
    Q_INVOKABLE bool setParam(
            const QString& nodeId, int paramId, double value);
    Q_INVOKABLE void clear();

    /// Reads the most recent master-output window for visualization. Requires
    /// `audio.scope`. Returns { sampleRate, frames, left, right, mono } with
    /// float arrays in [-1, 1], or an empty object when unavailable.
    Q_INVOKABLE QVariantMap readScope() const;

  private:
    PluginApi* m_pApi;
};

/// mixxx.download — install plugins from a URL.
class DownloadProxy : public QObject {
    Q_OBJECT
  public:
    explicit DownloadProxy(PluginApi* pApi);

    /// callback(result) with { ok, pluginId, error }.
    Q_INVOKABLE void installFromUrl(
            const QString& url, const QString& sha256, const QJSValue& callback);

  private:
    PluginApi* m_pApi;
    /// Pending install callbacks, kept here so their QJSValues are destroyed
    /// with this proxy (while the engine is still alive) rather than after the
    /// plugin's engine has been torn down.
    QHash<quint64, QJSValue> m_pendingInstalls;
    quint64 m_installCounter = 0;
};

/// mixxx.crypto — small hashing helpers for authentication schemes.
class CryptoProxy : public QObject {
    Q_OBJECT
  public:
    explicit CryptoProxy(PluginApi* pApi);

    /// Lowercase hex MD5 of the UTF-8 encoded text.
    Q_INVOKABLE QString md5(const QString& text) const;
    /// A random lowercase hex string with 2 * byteCount characters.
    Q_INVOKABLE QString randomHex(int byteCount) const;

  private:
    PluginApi* m_pApi;
};

/// mixxx.remote — register remote music sources rendered in the library.
class RemoteProxy : public QObject {
    Q_OBJECT
  public:
    explicit RemoteProxy(PluginApi* pApi);

    /// spec: { name, search(query, offset, limit, cb),
    ///         resolve(id, cb), download?, isDownloaded?, removeDownload?,
    ///         coverArt?, count? }. Returns the source id.
    Q_INVOKABLE QString addSource(const QJSValue& spec);
    Q_INVOKABLE void removeSource(const QString& sourceId);
    Q_INVOKABLE void refresh(const QString& sourceId);
    Q_INVOKABLE void reportProgress(const QString& sourceId,
            const QString& trackId,
            double received,
            double total);
    Q_INVOKABLE void removeProgress(const QString& sourceId, const QString& trackId);

    /// opts: { sourceId, text, callback(trackIds) }. Returns the action id.
    Q_INVOKABLE QString addTrackAction(const QJSValue& options);
    Q_INVOKABLE void removeTrackAction(const QString& actionId);

  private:
    PluginApi* m_pApi;
    /// Counter for generating unique track-action ids for this proxy.
    quint64 m_actionCounter = 0;
};

} // namespace plugins
} // namespace mixxx
