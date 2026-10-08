#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "preferences/usersettings.h"

class QNetworkAccessManager;
class QUrl;

namespace mixxx {
namespace plugins {

/// Installs plugins from a local directory or archive and from a URL.
///
/// Verification: every install is validated by parsing plugin.json and, when an
/// expected SHA-256 is supplied for a download, by comparing the archive
/// digest. Cryptographic *signature* verification is not implemented yet (no
/// key infrastructure is shipped with this fork); the docs call this out.
class PluginStore : public QObject {
    Q_OBJECT
  public:
    PluginStore(QNetworkAccessManager* pNetworkManager,
            UserSettingsPointer pConfig,
            QObject* pParent = nullptr);

    /// Copies a plugin directory into the user plugin folder. The source
    /// directory must contain a valid plugin.json.
    bool installFromDirectory(
            const QString& sourceDirectory, QString* pPluginId, QString* pError);

    /// Extracts a .zip (or .tar.gz / .tgz) archive into a temporary directory
    /// and installs the plugin it contains.
    bool installFromArchive(
            const QString& archivePath, QString* pPluginId, QString* pError);

    using InstallCallback = std::function<void(bool success,
            const QString& pluginId,
            const QString& error)>;

    /// Downloads `url` to a temporary file, verifies the optional SHA-256 and
    /// installs the plugin. The callback is invoked on the GUI thread.
    void installFromUrl(const QUrl& url,
            const QString& expectedSha256,
            InstallCallback callback);

    /// Path where user plugins are installed: ~/.mixxx/plugins
    QString userPluginDir() const;

  signals:
    void installed(const QString& pluginId);

  private:
    bool installExtractedDirectory(const QString& extractedDirectory,
            QString* pPluginId,
            QString* pError);
    QString findManifestDirectory(const QString& root) const;

    QNetworkAccessManager* const m_pNetworkManager;
    UserSettingsPointer m_pConfig;
};

} // namespace plugins
} // namespace mixxx
