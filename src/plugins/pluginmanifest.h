#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVersionNumber>
#include <optional>

#include "plugins/pluginpermissions.h"
#include "version.h"

namespace mixxx {
namespace plugins {

/// Parsed representation of a plugin's `plugin.json` manifest.
///
/// Example:
/// {
///   "id": "com.example.gain-boost",
///   "name": "Gain Boost",
///   "description": "A tiny gain stage.",
///   "author": "You",
///   "version": "1.0.0",
///   "apiVersion": 1,
///   "runtime": "script",              // or "native"
///   "entry": { "script": "main.js", "native": "libgainboost.so" },
///   "capabilities": ["audio.graph"],
///   "minMixxxVersion": "2.6"
/// }
struct PluginManifest {
    enum class Runtime {
        Script,
        Native,
    };

    QString id;
    QString name;
    QString description;
    QString author;
    QString version;
    QString homepage;

    int apiVersion = 0;
    Runtime runtime = Runtime::Script;

    /// Path (relative to the plugin directory) of the script/native entry.
    QString scriptEntry;
    QString nativeEntry;

    /// Capabilities requested by the manifest. Unknown capability strings are
    /// kept separately so the preferences page can surface manifest typos.
    PluginCapabilitySet requestedCapabilities;
    QStringList unknownCapabilities;

    QVersionNumber minMixxxVersion;
    QVersionNumber maxMixxxVersion;

    /// Whether the manifest passed basic validation (id, runtime, entry, ...).
    bool isValid = false;

    /// Returns the entry path for the plugin's runtime, or an empty string.
    QString entryFile() const;

    QString runtimeString() const;
};

class PluginManifestReader {
  public:
    /// The plugin API version implemented by this build. Manifests declaring a
    /// higher version are rejected.
    static constexpr int kCurrentApiVersion = MIXXX_PLUGIN_API_VERSION;

    /// The oldest plugin API version this build still loads. Manifests
    /// declaring a lower version are rejected.
    static constexpr int kMinSupportedApiVersion = MIXXX_PLUGIN_API_MIN_VERSION;

    /// Parses manifest bytes. On failure returns std::nullopt and (if not
    /// nullptr) stores a human-readable reason in `errorMessage`.
    static std::optional<PluginManifest> parse(
            const QByteArray& data, QString* errorMessage = nullptr);

    /// Reads and parses `plugin.json` at `filePath`.
    static std::optional<PluginManifest> read(
            const QString& filePath, QString* errorMessage = nullptr);

    /// Checks API and Mixxx version compatibility. Returns false and sets
    /// `errorMessage` when the plugin cannot run on this build.
    static bool isSupportedByThisBuild(
            const PluginManifest& manifest, QString* errorMessage = nullptr);
};

} // namespace plugins
} // namespace mixxx
