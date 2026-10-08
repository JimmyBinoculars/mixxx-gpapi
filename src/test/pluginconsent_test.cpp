#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include "plugins/pluginpermissions.h"
#include "preferences/constants.h"

namespace mixxx {
namespace plugins {

namespace {

UserSettingsPointer settingsFor(const QTemporaryDir& dir) {
    const QString file = QDir(dir.path()).filePath(QStringLiteral("test.cfg"));
    return UserSettingsPointer::create(file);
}

} // namespace

// A fresh, isolated config file per test so persisted state cannot leak.
class PluginConsentTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_dir.isValid());
    }

    UserSettingsPointer freshSettings() const {
        return settingsFor(m_dir);
    }

  private:
    QTemporaryDir m_dir;
};

// Granting a capability records it as both granted and consented.
TEST_F(PluginConsentTest, GrantRecordsConsent) {
    PluginPermissionStore store(freshSettings());
    const QString id = QStringLiteral("com.example.p");

    EXPECT_FALSE(store.consentedCapabilities(id).testFlag(PluginCapability::UiMenu));

    store.setGrantedCapabilities(id,
            PluginCapabilitySet(PluginCapability::UiMenu) |
                    PluginCapability::Network);

    EXPECT_TRUE(store.grantedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(store.consentedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(store.consentedCapabilities(id).testFlag(PluginCapability::Network));
}

// The user reducing the granted set must keep the capability "consented", so a
// later load does not re-prompt (and cannot accidentally disable the plugin).
TEST_F(PluginConsentTest, ReducingGrantedKeepsConsented) {
    PluginPermissionStore store(freshSettings());
    const QString id = QStringLiteral("com.example.p");

    store.setGrantedCapabilities(id,
            PluginCapabilitySet(PluginCapability::UiMenu) |
                    PluginCapability::ControlsRead);
    store.setGrantedCapabilities(id, PluginCapabilitySet(PluginCapability::ControlsRead));

    EXPECT_FALSE(store.grantedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(store.consentedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(store.grantedCapabilities(id).testFlag(PluginCapability::ControlsRead));
}

// setConsentedCapabilities() records approval without granting the capability.
TEST_F(PluginConsentTest, ConsentedWithoutGranting) {
    PluginPermissionStore store(freshSettings());
    const QString id = QStringLiteral("com.example.p");

    store.setGrantedCapabilities(id, PluginCapabilitySet(PluginCapability::UiMenu));
    store.setConsentedCapabilities(id, PluginCapabilitySet(PluginCapability::Network));

    EXPECT_TRUE(store.grantedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_FALSE(store.grantedCapabilities(id).testFlag(PluginCapability::Network));
    EXPECT_TRUE(store.consentedCapabilities(id).testFlag(PluginCapability::Network));
}

// Configs written before ConsentedCapabilities existed fall back to the granted
// set so existing users are not re-prompted.
TEST_F(PluginConsentTest, MigrationFallsBackToGranted) {
    UserSettingsPointer settings = freshSettings();
    settings->setValue(
            ConfigKey(QStringLiteral("[Plugin:com.example.old]"),
                    QStringLiteral("Permissions")),
            QStringLiteral("ui.menu,network"));

    PluginPermissionStore store(settings);
    const QString id = QStringLiteral("com.example.old");
    EXPECT_TRUE(store.consentedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(store.consentedCapabilities(id).testFlag(PluginCapability::Network));
}

TEST_F(PluginConsentTest, EmptyGrantRecordsNothing) {
    PluginPermissionStore store(freshSettings());
    const QString id = QStringLiteral("com.example.p");

    store.setGrantedCapabilities(id, PluginCapabilitySet());

    EXPECT_EQ(store.grantedCapabilities(id), PluginCapabilitySet());
    EXPECT_EQ(store.consentedCapabilities(id), PluginCapabilitySet());
}

TEST_F(PluginConsentTest, AllowedPathsRoundTrip) {
    PluginPermissionStore store(freshSettings());
    const QString id = QStringLiteral("com.example.p");

    EXPECT_TRUE(store.allowedPaths(id).isEmpty());
    store.addAllowedPath(id, QStringLiteral("/home/user/Music"));
    store.addAllowedPath(id, QStringLiteral("/home/user/Music")); // deduped
    store.addAllowedPath(id, QStringLiteral("/tmp/downloads"));
    EXPECT_EQ(store.allowedPaths(id).size(), 2);
    EXPECT_TRUE(store.allowedPaths(id).contains(QStringLiteral("/home/user/Music")));

    store.removeAllowedPath(id, QStringLiteral("/tmp/downloads"));
    EXPECT_EQ(store.allowedPaths(id), QStringList{QStringLiteral("/home/user/Music")});
}

TEST_F(PluginConsentTest, RemovePluginClearsStoredState) {
    PluginPermissionStore store(freshSettings());
    const QString id = QStringLiteral("com.example.p");
    store.setEnabled(id, false);
    store.grant(id, PluginCapability::UiMenu);
    store.setConsentShown(id, true);
    store.addAllowedPath(id, QStringLiteral("/tmp/downloads"));

    store.removePlugin(id);

    EXPECT_TRUE(store.isEnabled(id)); // enabled again by default
    EXPECT_FALSE(store.isConsentShown(id));
    EXPECT_FALSE(store.grantedCapabilities(id).testFlag(PluginCapability::UiMenu));
    EXPECT_TRUE(store.allowedPaths(id).isEmpty());
}

} // namespace plugins
} // namespace mixxx
