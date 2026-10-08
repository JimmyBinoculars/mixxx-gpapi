#pragma once

#include <QFlags>
#include <QString>
#include <QStringList>

#include "preferences/usersettings.h"

namespace mixxx {
namespace plugins {

/// Capabilities a plugin can request in its manifest and that the host gates at
/// runtime. The values are serialized by their stable string keys, so the
/// numeric values must never be reused for a different capability.
enum class PluginCapability {
    Invalid = 0,

    /// Read ControlObject values through `mixxx.controls`.
    ControlsRead = 1 << 0,
    /// Write ControlObject values through `mixxx.controls`.
    ControlsWrite = 1 << 1,

    /// Add items to the main menu bar through `mixxx.menu`.
    UiMenu = 1 << 2,
    /// Add panels/dock widgets, notifications and dialogs through `mixxx.ui`.
    UiPanel = 1 << 3,

    /// Perform HTTP requests through `mixxx.net`.
    Network = 1 << 4,

    /// Read files inside the plugin's own directory through `mixxx.files`.
    FilesRead = 1 << 5,
    /// Write files inside the plugin's own directory through `mixxx.files`.
    FilesWrite = 1 << 6,

    /// Query the track library through `mixxx.library`.
    LibraryRead = 1 << 7,
    /// Modify tracks / load tracks through `mixxx.library`.
    LibraryWrite = 1 << 8,

    /// Attach nodes to the real-time audio graph through `mixxx.audio`.
    AudioGraph = 1 << 9,

    /// Install or update other plugins from a URL through `mixxx.download`.
    Download = 1 << 10,

    /// Run external programs through `mixxx.process`.
    Exec = 1 << 11,

    /// Read/write files outside the plugin's own directory through
    /// `mixxx.files` (paths must be approved, e.g. via a folder picker).
    FilesPaths = 1 << 12,

    /// Render web (WebGL) content inside a plugin panel/window through
    /// `mixxx.ui` (`web` panel option). Requires a build with Qt WebEngine.
    UiWeb = 1 << 13,

    /// Read the master output audio for visualization through `mixxx.audio`
    /// (`audio` panel option / `mixxx.audio.readScope()`).
    AudioScope = 1 << 14,

    /// Register a remote music source with the library and resolve its tracks
    /// on demand through `mixxx.remote`.
    LibraryRemote = 1 << 15,
};

/// Returns a stable, human-readable identifier (e.g. "controls.read").
QString capabilityToString(PluginCapability capability);

/// Parses a manifest capability string. Returns Invalid for unknown strings.
PluginCapability capabilityFromString(const QString& key);

/// User-facing description used in the consent dialog and preferences page.
QString capabilityDescription(PluginCapability capability);

/// A set of capabilities.
using PluginCapabilitySet = QFlags<PluginCapability>;

/// Returns every known capability. Mainly used by the developer REPL, which
/// bypasses gating anyway.
PluginCapabilitySet allCapabilities();

/// Per-plugin grant storage backed by mixxx.cfg.
///
/// Layout (all under a single group per plugin so it is easy to delete):
///   [Plugin:<id>]
///   Enabled=1
///   Permissions=controls.read,ui.menu
///   ConsentedCapabilities=controls.read,ui.menu
///   ConsentShown=1
class PluginPermissionStore {
  public:
    explicit PluginPermissionStore(UserSettingsPointer pConfig);

    bool isEnabled(const QString& pluginId) const;
    void setEnabled(const QString& pluginId, bool enabled);

    bool isConsentShown(const QString& pluginId) const;
    void setConsentShown(const QString& pluginId, bool shown);

    /// All capabilities the user has explicitly granted, regardless of whether
    /// they are still requested by the manifest.
    PluginCapabilitySet grantedCapabilities(const QString& pluginId) const;

    /// The union of every capability the user has ever approved for the plugin
    /// at a consent prompt. Used to decide when a manifest change requires a
    /// new prompt: only capabilities outside this set are considered "new".
    ///
    /// This is deliberately independent from `grantedCapabilities()`: the user
    /// may later reduce the granted set (or a plugin may stop requesting a
    /// capability) without triggering another consent prompt.
    PluginCapabilitySet consentedCapabilities(const QString& pluginId) const;

    /// Grants/sets the complete set of capabilities for a plugin. Also records
    /// the given capabilities as consented, so approving a subset does not
    /// re-prompt next time.
    void setGrantedCapabilities(
            const QString& pluginId, PluginCapabilitySet capabilities);

    /// Marks capabilities as consented without changing the granted set.
    void setConsentedCapabilities(
            const QString& pluginId, PluginCapabilitySet capabilities);

    /// Filesystem roots (absolute, canonical) the plugin may access outside its
    /// own directory. Populated when the user explicitly approves a path (e.g.
    /// via `mixxx.ui.pickFolder()`).
    QStringList allowedPaths(const QString& pluginId) const;
    void setAllowedPaths(const QString& pluginId, const QStringList& paths);
    void addAllowedPath(const QString& pluginId, const QString& path);
    void removeAllowedPath(const QString& pluginId, const QString& path);

    void grant(const QString& pluginId, PluginCapability capability);
    void revoke(const QString& pluginId, PluginCapability capability);

    /// True if the user has granted the capability to the plugin. Whether the
    /// manifest requests it is checked separately by the caller (`PluginApi`).
    bool isGranted(const QString& pluginId,
            PluginCapability capability) const;

    /// Removes all stored state for a plugin (called on uninstall).
    void removePlugin(const QString& pluginId);

  private:
    static QString groupFor(const QString& pluginId);

    UserSettingsPointer m_pConfig;
};

} // namespace plugins
} // namespace mixxx
