#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include "library/libraryfeature.h"
#include "library/treeitemmodel.h"
#include "util/parented_ptr.h"

namespace mixxx {
namespace plugins {

class RemoteSourceManager;
class PluginRemoteTrackModel;

/// Renders every remote music source registered through `mixxx.remote` as a
/// child of a single sidebar node and displays its tracks in the shared track
/// table. Loading a track resolves (downloads) it first.
class PluginRemoteLibraryFeature : public LibraryFeature {
    Q_OBJECT
  public:
    PluginRemoteLibraryFeature(
            Library* pLibrary, UserSettingsPointer pConfig);
    ~PluginRemoteLibraryFeature() override = default;

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;

    bool hasTrackTable() override {
        return true;
    }

    /// Binds the manager that owns the plugin sources. May be nullptr.
    void setRemoteSourceManager(RemoteSourceManager* pManager);

    /// True if `location` was produced by makeLocation() for any source.
    bool isRemoteLocation(const QString& location) const;

    /// Resolves a remote location and loads the resulting local file into
    /// `group` (or, when `toNextAvailableDeck` is true, into the next available
    /// deck via the normal Library::loadTrack path).
    void loadRemoteTrack(const QString& location,
            const QString& group,
            bool play,
            bool toNextAvailableDeck = false);

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;

  private slots:
    void rebuildSidebar();
    void onRefreshRequested(const QString& sourceId);
    void onProgressChanged(const QString& sourceId,
            const QString& trackId,
            qint64 received,
            qint64 total);

  private:
    PluginRemoteTrackModel* modelForSource(const QString& sourceId);
    void showSource(const QString& sourceId);
    void showFallbackModel();

    /// Known tags (title, artist, ...) for a remote track, taken from the model
    /// that is currently displaying it. Empty if the model no longer has it.
    QVariantMap metadataFor(const QString& sourceId, const QString& trackId) const;

    QPointer<RemoteSourceManager> m_pManager;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    QHash<QString, PluginRemoteTrackModel*> m_models;
    /// Fallback model shown when no source is available. Parented to `this`,
    /// so it lives as long as the feature and may be shown repeatedly.
    PluginRemoteTrackModel* m_pEmptyModel = nullptr;
    QString m_lastSourceId;
    /// Deck group awaiting a download, keyed by (sourceId, trackId), so byte
    /// progress can be routed to the right deck's overview.
    QHash<QString, QString> m_downloadGroupByTrack;
};

} // namespace plugins
} // namespace mixxx
