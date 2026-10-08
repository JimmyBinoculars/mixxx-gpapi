#include "plugins/pluginmanifest.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include "util/versionstore.h"

namespace mixxx {
namespace plugins {

namespace {

bool isValidPluginId(const QString& id) {
    static const QRegularExpression re(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return re.match(id).hasMatch();
}

QString parseEntry(const QJsonObject& entryObject,
        const QJsonValue& entryValue,
        const QString& key) {
    if (entryObject.contains(key) && entryObject.value(key).isString()) {
        return entryObject.value(key).toString();
    }
    // Also accept a flat "entry": "main.js" for the script runtime.
    if (key == QLatin1String("script") && entryValue.isString()) {
        return entryValue.toString();
    }
    return QString();
}

} // anonymous namespace

QString PluginManifest::entryFile() const {
    switch (runtime) {
    case Runtime::Native:
        return nativeEntry;
    case Runtime::Script:
    default:
        return scriptEntry;
    }
}

QString PluginManifest::runtimeString() const {
    switch (runtime) {
    case Runtime::Native:
        return QStringLiteral("native");
    case Runtime::Script:
    default:
        return QStringLiteral("script");
    }
}

std::optional<PluginManifest> PluginManifestReader::parse(
        const QByteArray& data, QString* errorMessage) {
    const auto fail = [errorMessage](const QString& reason) {
        if (errorMessage != nullptr) {
            *errorMessage = reason;
        }
        return std::nullopt;
    };

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("Invalid JSON: %1 (offset %2)")
                            .arg(jsonError.errorString())
                            .arg(jsonError.offset));
    }
    if (!doc.isObject()) {
        return fail(QStringLiteral("Manifest root must be a JSON object."));
    }

    const QJsonObject root = doc.object();

    PluginManifest manifest;
    manifest.id = root.value(QStringLiteral("id")).toString().trimmed();
    manifest.name = root.value(QStringLiteral("name")).toString().trimmed();
    manifest.description = root.value(QStringLiteral("description")).toString();
    manifest.author = root.value(QStringLiteral("author")).toString();
    manifest.version = root.value(QStringLiteral("version")).toString();
    manifest.homepage = root.value(QStringLiteral("homepage")).toString();
    manifest.apiVersion = root.value(QStringLiteral("apiVersion")).toInt(0);

    const QString runtime = root.value(QStringLiteral("runtime"))
                                    .toString(QStringLiteral("script"))
                                    .trimmed()
                                    .toLower();
    if (runtime == QLatin1String("native")) {
        manifest.runtime = PluginManifest::Runtime::Native;
    } else if (runtime == QLatin1String("script")) {
        manifest.runtime = PluginManifest::Runtime::Script;
    } else {
        return fail(QStringLiteral("Unknown runtime '%1' (expected 'script' or 'native').")
                            .arg(runtime));
    }

    const QJsonValue entryValue = root.value(QStringLiteral("entry"));
    if (entryValue.isObject()) {
        const QJsonObject entryObject = entryValue.toObject();
        manifest.scriptEntry = parseEntry(entryObject, entryValue, QStringLiteral("script"));
        manifest.nativeEntry = parseEntry(entryObject, entryValue, QStringLiteral("native"));
    } else if (entryValue.isString()) {
        // Convenience: a bare string is treated as the script entry.
        manifest.scriptEntry = entryValue.toString();
    }

    const QJsonArray capabilities = root.value(QStringLiteral("capabilities")).toArray();
    for (const QJsonValue& value : capabilities) {
        if (!value.isString()) {
            continue;
        }
        const QString key = value.toString().trimmed();
        const PluginCapability capability = capabilityFromString(key);
        if (capability == PluginCapability::Invalid) {
            manifest.unknownCapabilities.append(key);
        } else {
            manifest.requestedCapabilities |= capability;
        }
    }

    const QString minVersion = root.value(QStringLiteral("minMixxxVersion"))
                                       .toString()
                                       .trimmed();
    if (!minVersion.isEmpty()) {
        manifest.minMixxxVersion = QVersionNumber::fromString(minVersion);
    }
    const QString maxVersion = root.value(QStringLiteral("maxMixxxVersion"))
                                       .toString()
                                       .trimmed();
    if (!maxVersion.isEmpty()) {
        manifest.maxMixxxVersion = QVersionNumber::fromString(maxVersion);
    }

    // --- Validation ---
    if (manifest.id.isEmpty()) {
        return fail(QStringLiteral("Missing required key 'id'."));
    }
    if (!isValidPluginId(manifest.id)) {
        return fail(QStringLiteral(
                "Invalid plugin id '%1'. Use letters, digits, '.', '_' and '-'.")
                            .arg(manifest.id));
    }
    if (manifest.name.isEmpty()) {
        return fail(QStringLiteral("Missing required key 'name'."));
    }
    if (manifest.apiVersion <= 0) {
        return fail(QStringLiteral("Missing or invalid required key 'apiVersion'."));
    }
    if (manifest.entryFile().isEmpty()) {
        return fail(QStringLiteral(
                            "Missing required '%1' entry for runtime '%2'.")
                            .arg(manifest.runtimeString() == QLatin1String("native")
                                            ? QStringLiteral("native")
                                            : QStringLiteral("script"),
                                    manifest.runtimeString()));
    }
    if (QDir::isAbsolutePath(manifest.entryFile()) ||
            manifest.entryFile().contains(QLatin1String(".."))) {
        return fail(QStringLiteral(
                "Entry path '%1' must be a relative path inside the plugin directory.")
                            .arg(manifest.entryFile()));
    }

    manifest.isValid = true;
    return manifest;
}

std::optional<PluginManifest> PluginManifestReader::read(
        const QString& filePath, QString* errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Cannot open '%1': %2")
                                    .arg(filePath, file.errorString());
        }
        return std::nullopt;
    }
    return parse(file.readAll(), errorMessage);
}

bool PluginManifestReader::isSupportedByThisBuild(
        const PluginManifest& manifest, QString* errorMessage) {
    const auto fail = [errorMessage](const QString& reason) {
        if (errorMessage != nullptr) {
            *errorMessage = reason;
        }
        return false;
    };

    if (manifest.apiVersion > kCurrentApiVersion) {
        return fail(QStringLiteral(
                            "Plugin requires plugin API v%1 but this build supports v%2.")
                            .arg(manifest.apiVersion)
                            .arg(kCurrentApiVersion));
    }

    if (manifest.apiVersion < kMinSupportedApiVersion) {
        return fail(QStringLiteral(
                            "Plugin targets plugin API v%1, which is no longer "
                            "supported (this build requires v%2 or newer).")
                            .arg(manifest.apiVersion)
                            .arg(kMinSupportedApiVersion));
    }

    const QVersionNumber current = VersionStore::versionNumber();
    if (!manifest.minMixxxVersion.isNull() && current < manifest.minMixxxVersion) {
        return fail(QStringLiteral("Plugin requires Mixxx %1 or newer (running %2).")
                            .arg(manifest.minMixxxVersion.toString(),
                                    current.toString()));
    }
    if (!manifest.maxMixxxVersion.isNull() && current > manifest.maxMixxxVersion) {
        return fail(QStringLiteral("Plugin supports Mixxx up to %1 (running %2).")
                            .arg(manifest.maxMixxxVersion.toString(),
                                    current.toString()));
    }
    return true;
}

} // namespace plugins
} // namespace mixxx
