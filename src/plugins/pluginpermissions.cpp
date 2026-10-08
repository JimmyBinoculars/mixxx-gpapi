#include "plugins/pluginpermissions.h"

#include <QHash>
#include <QObject>

#include "preferences/constants.h"

namespace mixxx {
namespace plugins {

namespace {

const QHash<PluginCapability, QString> kCapabilityKeys = {
        {PluginCapability::ControlsRead, QStringLiteral("controls.read")},
        {PluginCapability::ControlsWrite, QStringLiteral("controls.write")},
        {PluginCapability::UiMenu, QStringLiteral("ui.menu")},
        {PluginCapability::UiPanel, QStringLiteral("ui.panel")},
        {PluginCapability::Network, QStringLiteral("network")},
        {PluginCapability::FilesRead, QStringLiteral("files.read")},
        {PluginCapability::FilesWrite, QStringLiteral("files.write")},
        {PluginCapability::LibraryRead, QStringLiteral("library.read")},
        {PluginCapability::LibraryWrite, QStringLiteral("library.write")},
        {PluginCapability::AudioGraph, QStringLiteral("audio.graph")},
        {PluginCapability::Download, QStringLiteral("download")},
        {PluginCapability::Exec, QStringLiteral("process.exec")},
        {PluginCapability::FilesPaths, QStringLiteral("files.paths")},
        {PluginCapability::UiWeb, QStringLiteral("ui.web")},
        {PluginCapability::AudioScope, QStringLiteral("audio.scope")},
        {PluginCapability::LibraryRemote, QStringLiteral("library.remote")},
};

QString serializeCapabilities(PluginCapabilitySet capabilities) {
    QStringList keys;
    for (auto it = kCapabilityKeys.constBegin(); it != kCapabilityKeys.constEnd(); ++it) {
        if (capabilities.testFlag(it.key())) {
            keys.append(it.value());
        }
    }
    keys.sort();
    return keys.join(',');
}

PluginCapabilitySet parseCapabilities(const QString& serialized) {
    PluginCapabilitySet result;
    const QStringList keys = serialized.split(',', Qt::SkipEmptyParts);
    for (const QString& key : keys) {
        const PluginCapability capability = capabilityFromString(key.trimmed());
        if (capability != PluginCapability::Invalid) {
            result |= capability;
        }
    }
    return result;
}

} // anonymous namespace

QString capabilityToString(PluginCapability capability) {
    return kCapabilityKeys.value(capability);
}

PluginCapability capabilityFromString(const QString& key) {
    for (auto it = kCapabilityKeys.constBegin(); it != kCapabilityKeys.constEnd(); ++it) {
        if (it.value().compare(key, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }
    return PluginCapability::Invalid;
}

QString capabilityDescription(PluginCapability capability) {
    switch (capability) {
    case PluginCapability::ControlsRead:
        return QObject::tr("Read the value of Mixxx controls (decks, mixer, ...).");
    case PluginCapability::ControlsWrite:
        return QObject::tr("Change the value of Mixxx controls, e.g. play/pause decks.");
    case PluginCapability::UiMenu:
        return QObject::tr("Add entries to the Mixxx menu bar.");
    case PluginCapability::UiPanel:
        return QObject::tr("Add panels, notifications and dialogs to the Mixxx window.");
    case PluginCapability::Network:
        return QObject::tr("Perform network requests on your behalf.");
    case PluginCapability::FilesRead:
        return QObject::tr("Read files inside the plugin's own directory.");
    case PluginCapability::FilesWrite:
        return QObject::tr("Write files inside the plugin's own directory.");
    case PluginCapability::LibraryRead:
        return QObject::tr("Read metadata from your track library.");
    case PluginCapability::LibraryWrite:
        return QObject::tr("Modify your track library and load tracks into decks.");
    case PluginCapability::AudioGraph:
        return QObject::tr(
                "Insert itself into the real-time audio processing graph.");
    case PluginCapability::Download:
        return QObject::tr("Install or update plugins from the internet.");
    case PluginCapability::Exec:
        return QObject::tr("Run external programs on your computer.");
    case PluginCapability::FilesPaths:
        return QObject::tr(
                "Read and write files in folders you approve (e.g. your music "
                "library).");
    case PluginCapability::UiWeb:
        return QObject::tr(
                "Open embedded web windows (e.g. WebGL visualizations).");
    case PluginCapability::AudioScope:
        return QObject::tr(
                "Read the master output audio for visualization.");
    case PluginCapability::LibraryRemote:
        return QObject::tr(
                "Add an internet music source to the library and download its "
                "tracks on demand.");
    case PluginCapability::Invalid:
    default:
        return QString();
    }
}

PluginCapabilitySet allCapabilities() {
    PluginCapabilitySet result;
    for (auto it = kCapabilityKeys.constBegin(); it != kCapabilityKeys.constEnd(); ++it) {
        result |= it.key();
    }
    return result;
}

PluginPermissionStore::PluginPermissionStore(UserSettingsPointer pConfig)
        : m_pConfig(std::move(pConfig)) {
}

QString PluginPermissionStore::groupFor(const QString& pluginId) {
    return QStringLiteral("[Plugin:%1]").arg(pluginId);
}

bool PluginPermissionStore::isEnabled(const QString& pluginId) const {
    return m_pConfig->getValue<bool>(
            ConfigKey(groupFor(pluginId), QStringLiteral("Enabled")), true);
}

void PluginPermissionStore::setEnabled(const QString& pluginId, bool enabled) {
    m_pConfig->setValue(
            ConfigKey(groupFor(pluginId), QStringLiteral("Enabled")), enabled);
}

bool PluginPermissionStore::isConsentShown(const QString& pluginId) const {
    return m_pConfig->getValue<bool>(
            ConfigKey(groupFor(pluginId), QStringLiteral("ConsentShown")), false);
}

void PluginPermissionStore::setConsentShown(const QString& pluginId, bool shown) {
    m_pConfig->setValue(
            ConfigKey(groupFor(pluginId), QStringLiteral("ConsentShown")), shown);
}

PluginCapabilitySet PluginPermissionStore::grantedCapabilities(
        const QString& pluginId) const {
    return parseCapabilities(m_pConfig->getValueString(
            ConfigKey(groupFor(pluginId), QStringLiteral("Permissions"))));
}

PluginCapabilitySet PluginPermissionStore::consentedCapabilities(
        const QString& pluginId) const {
    const QString raw = m_pConfig->getValueString(
            ConfigKey(groupFor(pluginId), QStringLiteral("ConsentedCapabilities")));
    if (!raw.isEmpty()) {
        return parseCapabilities(raw);
    }
    // Migration for plugins that were granted capabilities before this key
    // existed: treat the previously granted set as already consented so we do
    // not re-prompt on the next start.
    return grantedCapabilities(pluginId);
}

void PluginPermissionStore::setGrantedCapabilities(
        const QString& pluginId, PluginCapabilitySet capabilities) {
    m_pConfig->setValue(ConfigKey(groupFor(pluginId),
                                 QStringLiteral("Permissions")),
            serializeCapabilities(capabilities));
    // Any capability the user grants counts as consented, so reducing the set
    // later never triggers another prompt.
    setConsentedCapabilities(
            pluginId, consentedCapabilities(pluginId) | capabilities);
}

void PluginPermissionStore::setConsentedCapabilities(
        const QString& pluginId, PluginCapabilitySet capabilities) {
    m_pConfig->setValue(ConfigKey(groupFor(pluginId),
                                 QStringLiteral("ConsentedCapabilities")),
            serializeCapabilities(capabilities));
}

QStringList PluginPermissionStore::allowedPaths(const QString& pluginId) const {
    const QString raw = m_pConfig->getValueString(
            ConfigKey(groupFor(pluginId), QStringLiteral("AllowedPaths")));
    QStringList paths;
    const QStringList parts = raw.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            paths.append(trimmed);
        }
    }
    return paths;
}

void PluginPermissionStore::setAllowedPaths(
        const QString& pluginId, const QStringList& paths) {
    QStringList normalized;
    for (const QString& path : paths) {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
            normalized.append(trimmed);
        }
    }
    m_pConfig->setValue(
            ConfigKey(groupFor(pluginId), QStringLiteral("AllowedPaths")),
            normalized.join(QLatin1Char('|')));
}

void PluginPermissionStore::addAllowedPath(
        const QString& pluginId, const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QStringList paths = allowedPaths(pluginId);
    if (!paths.contains(trimmed)) {
        paths.append(trimmed);
        setAllowedPaths(pluginId, paths);
    }
}

void PluginPermissionStore::removeAllowedPath(
        const QString& pluginId, const QString& path) {
    QStringList paths = allowedPaths(pluginId);
    paths.removeAll(path.trimmed());
    setAllowedPaths(pluginId, paths);
}

void PluginPermissionStore::grant(
        const QString& pluginId, PluginCapability capability) {
    if (capability == PluginCapability::Invalid) {
        return;
    }
    PluginCapabilitySet granted = grantedCapabilities(pluginId);
    granted |= capability;
    setGrantedCapabilities(pluginId, granted);
}

void PluginPermissionStore::revoke(
        const QString& pluginId, PluginCapability capability) {
    PluginCapabilitySet granted = grantedCapabilities(pluginId);
    granted &= ~PluginCapabilitySet(capability);
    setGrantedCapabilities(pluginId, granted);
}

bool PluginPermissionStore::isGranted(
        const QString& pluginId, PluginCapability capability) const {
    return grantedCapabilities(pluginId).testFlag(capability);
}

void PluginPermissionStore::removePlugin(const QString& pluginId) {
    m_pConfig->remove(ConfigKey(groupFor(pluginId), QStringLiteral("Enabled")));
    m_pConfig->remove(ConfigKey(groupFor(pluginId), QStringLiteral("Permissions")));
    m_pConfig->remove(ConfigKey(groupFor(pluginId), QStringLiteral("ConsentedCapabilities")));
    m_pConfig->remove(ConfigKey(groupFor(pluginId), QStringLiteral("AllowedPaths")));
    m_pConfig->remove(ConfigKey(groupFor(pluginId), QStringLiteral("ConsentShown")));
}

} // namespace plugins
} // namespace mixxx
