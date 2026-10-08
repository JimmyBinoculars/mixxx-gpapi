#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include "plugins/pluginmanifest.h"
#include "plugins/pluginpermissions.h"

namespace mixxx {
namespace plugins {

TEST(PluginManifestTest, ParsesValidScriptManifest) {
    const QByteArray json = R"({
        "id": "com.example.hello",
        "name": "Hello",
        "description": "An example",
        "version": "1.2.3",
        "apiVersion": 1,
        "runtime": "script",
        "entry": { "script": "main.js" },
        "capabilities": ["ui.menu", "controls.read"],
        "minMixxxVersion": "2.0"
    })";

    QString error;
    const auto manifest = PluginManifestReader::parse(json, &error);
    ASSERT_TRUE(manifest.has_value()) << error.toStdString();
    EXPECT_TRUE(manifest->isValid);
    EXPECT_EQ(manifest->id, QStringLiteral("com.example.hello"));
    EXPECT_EQ(manifest->runtime, PluginManifest::Runtime::Script);
    EXPECT_EQ(manifest->entryFile(), QStringLiteral("main.js"));
    EXPECT_TRUE(manifest->requestedCapabilities.testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(manifest->requestedCapabilities.testFlag(PluginCapability::ControlsRead));
    EXPECT_FALSE(manifest->requestedCapabilities.testFlag(PluginCapability::Network));
    EXPECT_TRUE(manifest->unknownCapabilities.isEmpty());
    EXPECT_EQ(manifest->minMixxxVersion, QVersionNumber(2, 0));
}

TEST(PluginManifestTest, ParsesNativeManifest) {
    const QByteArray json = R"({
        "id": "com.example.gain",
        "name": "Gain",
        "apiVersion": 1,
        "runtime": "native",
        "entry": { "native": "libgain.so" },
        "capabilities": ["audio.graph"]
    })";

    QString error;
    const auto manifest = PluginManifestReader::parse(json, &error);
    ASSERT_TRUE(manifest.has_value()) << error.toStdString();
    EXPECT_EQ(manifest->runtime, PluginManifest::Runtime::Native);
    EXPECT_EQ(manifest->entryFile(), QStringLiteral("libgain.so"));
    EXPECT_TRUE(manifest->requestedCapabilities.testFlag(PluginCapability::AudioGraph));
}

TEST(PluginManifestTest, ReportsUnknownCapabilities) {
    const QByteArray json = R"({
        "id": "com.example.weird",
        "name": "Weird",
        "apiVersion": 1,
        "runtime": "script",
        "entry": { "script": "main.js" },
        "capabilities": ["ui.menu", "not.real"]
    })";

    QString error;
    const auto manifest = PluginManifestReader::parse(json, &error);
    ASSERT_TRUE(manifest.has_value()) << error.toStdString();
    EXPECT_EQ(manifest->unknownCapabilities,
            QStringList{QStringLiteral("not.real")});
}

TEST(PluginManifestTest, RejectsMissingId) {
    const QByteArray json = R"({
        "name": "No id",
        "apiVersion": 1,
        "runtime": "script",
        "entry": { "script": "main.js" }
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsMissingEntry) {
    const QByteArray json = R"({
        "id": "com.example.noentry",
        "name": "No entry",
        "apiVersion": 1,
        "runtime": "native"
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsUnknownRuntime) {
    const QByteArray json = R"({
        "id": "com.example.badruntime",
        "name": "Bad runtime",
        "apiVersion": 1,
        "runtime": "python",
        "entry": { "script": "main.py" }
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsEntryTraversal) {
    const QByteArray json = R"({
        "id": "com.example.traversal",
        "name": "Traversal",
        "apiVersion": 1,
        "runtime": "script",
        "entry": { "script": "../evil.js" }
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsInvalidId) {
    const QByteArray json = R"({
        "id": ".starts-with-dot",
        "name": "Bad id",
        "apiVersion": 1,
        "runtime": "script",
        "entry": { "script": "main.js" }
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsMissingName) {
    const QByteArray json = R"({
        "id": "com.example.noname",
        "apiVersion": 1,
        "runtime": "script",
        "entry": { "script": "main.js" }
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsMissingApiVersion) {
    const QByteArray json = R"({
        "id": "com.example.noapiversion",
        "name": "No api version",
        "runtime": "script",
        "entry": { "script": "main.js" }
    })";
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(json, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsMalformedJson) {
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(
                         QByteArrayLiteral("{ not json"), &error)
                        .has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsNonObjectRoot) {
    QString error;
    EXPECT_FALSE(PluginManifestReader::parse(QByteArrayLiteral("[1, 2, 3]"), &error)
                        .has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsApiVersionNewerThanBuild) {
    PluginManifest manifest;
    manifest.apiVersion = PluginManifestReader::kCurrentApiVersion + 1;
    QString error;
    EXPECT_FALSE(PluginManifestReader::isSupportedByThisBuild(manifest, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, AcceptsManifestWithoutVersionConstraints) {
    PluginManifest manifest;
    manifest.apiVersion = PluginManifestReader::kCurrentApiVersion;
    QString error;
    EXPECT_TRUE(PluginManifestReader::isSupportedByThisBuild(manifest, &error));
    EXPECT_TRUE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsTooNewMinMixxxVersion) {
    PluginManifest manifest;
    manifest.apiVersion = PluginManifestReader::kCurrentApiVersion;
    manifest.minMixxxVersion = QVersionNumber(9999, 0, 0);
    QString error;
    EXPECT_FALSE(PluginManifestReader::isSupportedByThisBuild(manifest, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginManifestTest, RejectsTooOldMaxMixxxVersion) {
    PluginManifest manifest;
    manifest.apiVersion = PluginManifestReader::kCurrentApiVersion;
    manifest.maxMixxxVersion = QVersionNumber(0, 0, 0);
    QString error;
    EXPECT_FALSE(PluginManifestReader::isSupportedByThisBuild(manifest, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(PluginCapabilityTest, RoundTripsStrings) {
    EXPECT_EQ(capabilityFromString(QStringLiteral("controls.read")),
            PluginCapability::ControlsRead);
    EXPECT_EQ(capabilityFromString(QStringLiteral("AUDIO.GRAPH")),
            PluginCapability::AudioGraph);
    EXPECT_EQ(capabilityFromString(QStringLiteral("nope")),
            PluginCapability::Invalid);
    EXPECT_EQ(capabilityToString(PluginCapability::LibraryWrite),
            QStringLiteral("library.write"));
    EXPECT_TRUE(allCapabilities().testFlag(PluginCapability::Download));
    EXPECT_TRUE(allCapabilities().testFlag(PluginCapability::AudioGraph));
}

TEST(PluginPermissionStoreTest, GrantAndRevoke) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("test.cfg"));
    UserSettingsPointer settings = UserSettingsPointer::create(file);
    PluginPermissionStore store(settings);

    // Enabled by default.
    EXPECT_TRUE(store.isEnabled(QStringLiteral("com.example")));
    EXPECT_FALSE(store.isConsentShown(QStringLiteral("com.example")));

    store.setEnabled(QStringLiteral("com.example"), false);
    EXPECT_FALSE(store.isEnabled(QStringLiteral("com.example")));
    store.setConsentShown(QStringLiteral("com.example"), true);
    EXPECT_TRUE(store.isConsentShown(QStringLiteral("com.example")));

    store.grant(QStringLiteral("com.example"), PluginCapability::UiMenu);
    store.grant(QStringLiteral("com.example"), PluginCapability::Network);
    PluginCapabilitySet granted = store.grantedCapabilities(QStringLiteral("com.example"));
    EXPECT_TRUE(granted.testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(granted.testFlag(PluginCapability::Network));
    EXPECT_FALSE(granted.testFlag(PluginCapability::AudioGraph));
    EXPECT_TRUE(store.isGranted(QStringLiteral("com.example"), PluginCapability::UiMenu));
    EXPECT_FALSE(store.isGranted(
            QStringLiteral("com.example"), PluginCapability::AudioGraph));

    store.revoke(QStringLiteral("com.example"), PluginCapability::UiMenu);
    granted = store.grantedCapabilities(QStringLiteral("com.example"));
    EXPECT_FALSE(granted.testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(granted.testFlag(PluginCapability::Network));

    store.setGrantedCapabilities(QStringLiteral("com.example"),
            PluginCapabilitySet(PluginCapability::LibraryRead) |
                    PluginCapability::LibraryWrite);
    granted = store.grantedCapabilities(QStringLiteral("com.example"));
    EXPECT_TRUE(granted.testFlag(PluginCapability::LibraryRead));
    EXPECT_TRUE(granted.testFlag(PluginCapability::LibraryWrite));
    EXPECT_FALSE(granted.testFlag(PluginCapability::Network));
}

} // namespace plugins
} // namespace mixxx
