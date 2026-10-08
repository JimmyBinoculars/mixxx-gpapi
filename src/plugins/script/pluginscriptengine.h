#pragma once

#include <QFileSystemWatcher>
#include <QJSValue>
#include <QObject>
#include <QString>

#include "plugins/pluginmanifest.h"

class QJSEngine;

namespace mixxx {
namespace plugins {

class PluginApi;
class PluginManager;

/// Runs a single script plugin in its own QJSEngine.
///
/// Contract implemented by `main.js`:
///     export function init(api) { ... }
///     export function shutdown() { ... }
///
/// The engine is intentionally NOT a security sandbox; capability gating in
/// PluginApi is best-effort protection against accidents and sloppy plugins.
class PluginScriptEngine : public QObject {
    Q_OBJECT
  public:
    PluginScriptEngine(PluginManifest manifest,
            QString pluginDirectory,
            PluginManager* pManager,
            QObject* pParent = nullptr);
    ~PluginScriptEngine() override;

    /// Loads the module and calls `init`.
    bool initialize();

    /// Calls `shutdown` and tears down the engine.
    void shutdown();

  signals:
    void beforeShutdown();

  private slots:
    void reload();

  private:
    void showError(const QString& title, const QString& message);

    PluginManifest m_manifest;
    QString m_pluginDirectory;
    QString m_entryPath;
    PluginManager* m_pManager;

    QJSEngine* m_pEngine = nullptr;
    PluginApi* m_pApi = nullptr;
    QJSValue m_shutdownFunction;

    QFileSystemWatcher m_fileWatcher;
};

} // namespace plugins
} // namespace mixxx
