#include "plugins/library/pluginremotelibraryfeature.h"

#include <QModelIndex>

#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "mixer/playermanager.h"
#include "plugins/library/pluginremotetrackmodel.h"
#include "plugins/library/remotesourcemanager.h"
#include "track/track.h"
#include "track/trackref.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginRemoteLibrary");

const char kNoSourcesLabel[] = QT_TRANSLATE_NOOP(
        "PluginRemoteLibraryFeature", "(no plugin sources)");

/// Key that ties a download's byte progress to the deck that requested it.
QString downloadKey(const QString& sourceId, const QString& trackId) {
    return sourceId + QLatin1Char('\n') + trackId;
}

} // anonymous namespace

PluginRemoteLibraryFeature::PluginRemoteLibraryFeature(
        Library* pLibrary, UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("computer")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)) {
    rebuildSidebar();
}

QVariant PluginRemoteLibraryFeature::title() {
    return tr("Online");
}

TreeItemModel* PluginRemoteLibraryFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void PluginRemoteLibraryFeature::setRemoteSourceManager(
        RemoteSourceManager* pManager) {
    if (m_pManager == pManager) {
        return;
    }
    if (m_pManager != nullptr) {
        disconnect(m_pManager, nullptr, this, nullptr);
    }
    m_pManager = pManager;
    if (m_pManager != nullptr) {
        connect(m_pManager,
                &RemoteSourceManager::sourcesChanged,
                this,
                &PluginRemoteLibraryFeature::rebuildSidebar);
        connect(m_pManager,
                &RemoteSourceManager::refreshRequested,
                this,
                &PluginRemoteLibraryFeature::onRefreshRequested);
        connect(m_pManager,
                &RemoteSourceManager::progressChanged,
                this,
                &PluginRemoteLibraryFeature::onProgressChanged);
    }
    rebuildSidebar();
}

void PluginRemoteLibraryFeature::rebuildSidebar() {
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    QStringList sourceIds;
    if (m_pManager != nullptr) {
        sourceIds = m_pManager->sourceIds();
    }
    if (sourceIds.isEmpty()) {
        pRootItem->appendChild(tr(kNoSourcesLabel));
    } else {
        for (const QString& sourceId : sourceIds) {
            const QString name = m_pManager->sourceName(sourceId);
            pRootItem->appendChild(name.isEmpty() ? sourceId : name, sourceId);
        }
    }
    m_pSidebarModel->setRootItem(std::move(pRootItem));
}

PluginRemoteTrackModel* PluginRemoteLibraryFeature::modelForSource(
        const QString& sourceId) {
    if (m_pManager == nullptr || !m_pManager->hasSource(sourceId)) {
        return nullptr;
    }
    auto it = m_models.constFind(sourceId);
    if (it != m_models.constEnd()) {
        return it.value();
    }
    auto* pModel = new PluginRemoteTrackModel(m_pManager, sourceId, this);
    m_models.insert(sourceId, pModel);
    return pModel;
}

void PluginRemoteLibraryFeature::showSource(const QString& sourceId) {
    PluginRemoteTrackModel* pModel = modelForSource(sourceId);
    if (pModel == nullptr) {
        showFallbackModel();
        return;
    }
    m_lastSourceId = sourceId;
    emit showTrackModel(pModel, true);
    emit restoreSearch(pModel->currentSearch());
    // Remote tracks have cover art too (fetched lazily from the source).
    emit enableCoverArtDisplay(true);
    // Populate the table for the first visit / after a source change.
    pModel->refresh();
}

void PluginRemoteLibraryFeature::showFallbackModel() {
    // Show the first available source, if any, so the view is never empty.
    if (m_pManager != nullptr) {
        const QStringList ids = m_pManager->sourceIds();
        if (!ids.isEmpty()) {
            showSource(ids.first());
            return;
        }
    }
    if (m_pEmptyModel == nullptr) {
        m_pEmptyModel = new PluginRemoteTrackModel(m_pManager, QString(), this);
    }
    emit showTrackModel(m_pEmptyModel, false);
    emit enableCoverArtDisplay(false);
}

void PluginRemoteLibraryFeature::activate() {
    if (m_pManager == nullptr) {
        showFallbackModel();
        return;
    }
    if (!m_lastSourceId.isEmpty() && m_pManager->hasSource(m_lastSourceId)) {
        showSource(m_lastSourceId);
        return;
    }
    const QStringList ids = m_pManager->sourceIds();
    if (!ids.isEmpty()) {
        showSource(ids.first());
    } else {
        showFallbackModel();
    }
}

void PluginRemoteLibraryFeature::activateChild(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    TreeItem* pItem = m_pSidebarModel->getItem(index);
    if (pItem == nullptr) {
        return;
    }
    const QString sourceId = pItem->getData().toString();
    if (!sourceId.isEmpty()) {
        showSource(sourceId);
    }
}

void PluginRemoteLibraryFeature::onRefreshRequested(const QString& sourceId) {
    auto it = m_models.constFind(sourceId);
    if (it != m_models.constEnd() && it.value() != nullptr) {
        it.value()->refresh();
    }
}

void PluginRemoteLibraryFeature::onProgressChanged(const QString& sourceId,
        const QString& trackId,
        qint64 received,
        qint64 total) {
    const QString group =
            m_downloadGroupByTrack.value(downloadKey(sourceId, trackId));
    if (group.isEmpty()) {
        // Not a deck-initiated download (e.g. "Download for offline").
        return;
    }
    if (received < 0) {
        // removeProgress(): the resolve callback clears the pending state.
        return;
    }
    PlayerManager* pPlayerManager = m_pLibrary->playerManager();
    if (pPlayerManager == nullptr) {
        return;
    }
    pPlayerManager->reportRemoteTrackDownloadProgress(group, received, total);
}

bool PluginRemoteLibraryFeature::isRemoteLocation(const QString& location) const {
    QString sourceId;
    if (!PluginRemoteTrackModel::parseLocation(location, &sourceId, nullptr)) {
        return false;
    }
    // Require the source to be registered so that a local file whose path
    // merely matches the remote prefix is not mistaken for a remote track.
    return m_pManager != nullptr && m_pManager->hasSource(sourceId);
}

QVariantMap PluginRemoteLibraryFeature::metadataFor(
        const QString& sourceId, const QString& trackId) const {
    QVariantMap metadata;
    PluginRemoteTrackModel* pModel = m_models.value(sourceId, nullptr);
    RemoteTrackData data;
    if (pModel == nullptr || !pModel->trackData(trackId, &data)) {
        return metadata;
    }
    if (!data.title.isEmpty()) {
        metadata.insert(QStringLiteral("title"), data.title);
    }
    if (!data.artist.isEmpty()) {
        metadata.insert(QStringLiteral("artist"), data.artist);
    }
    if (!data.album.isEmpty()) {
        metadata.insert(QStringLiteral("album"), data.album);
    }
    if (!data.albumArtist.isEmpty()) {
        metadata.insert(QStringLiteral("albumArtist"), data.albumArtist);
    }
    if (!data.genre.isEmpty()) {
        metadata.insert(QStringLiteral("genre"), data.genre);
    }
    if (!data.year.isEmpty()) {
        metadata.insert(QStringLiteral("year"), data.year);
    }
    if (!data.trackNumber.isEmpty()) {
        metadata.insert(QStringLiteral("trackNumber"), data.trackNumber);
    }
    if (data.duration > 0.0) {
        metadata.insert(QStringLiteral("duration"), data.duration);
    }
    return metadata;
}

void PluginRemoteLibraryFeature::loadRemoteTrack(const QString& location,
        const QString& group,
        bool play,
        bool toNextAvailableDeck) {
    QString sourceId;
    QString trackId;
    if (m_pManager == nullptr ||
            !PluginRemoteTrackModel::parseLocation(location, &sourceId, &trackId)) {
        kLogger.warning() << "Cannot resolve remote location" << location;
        return;
    }

    // Show the track's known tags and a "downloading" hint on the deck that
    // will receive it. For the "next available deck" path the deck is chosen
    // again once the file is ready, so we highlight the likely target.
    QString targetGroup = group;
    PlayerManager* pPlayerManager = m_pLibrary->playerManager();
    if (toNextAvailableDeck && pPlayerManager != nullptr) {
        targetGroup = pPlayerManager->nextAvailableDeckGroup();
    }
    const QString key = downloadKey(sourceId, trackId);
    if (pPlayerManager != nullptr && !targetGroup.isEmpty()) {
        m_downloadGroupByTrack.insert(key, targetGroup);
        pPlayerManager->reportRemoteTrackDownloadStarted(
                targetGroup, metadataFor(sourceId, trackId));
    }

    kLogger.debug() << "Resolving remote track" << sourceId << trackId;
    m_pManager->resolve(sourceId,
            trackId,
            [this, key, group, play, toNextAvailableDeck, targetGroup, pPlayerManager](
                    bool ok, const QString& path, const QString& error) {
                m_downloadGroupByTrack.remove(key);
                if (pPlayerManager != nullptr && !targetGroup.isEmpty()) {
                    pPlayerManager->reportRemoteTrackDownloadFinished(targetGroup);
                }
                if (!ok || path.isEmpty()) {
                    kLogger.warning()
                            << "Failed to resolve remote track:" << error;
                    return;
                }
                if (toNextAvailableDeck) {
                    // Reuse Mixxx's normal "load into the next available deck"
                    // path now that the file exists locally.
                    TrackPointer pTrack =
                            m_pLibrary->trackCollectionManager()->getOrAddTrack(
                                    TrackRef::fromFilePath(path));
                    if (pTrack) {
                        m_pLibrary->slotLoadTrack(pTrack);
                    }
                } else {
                    m_pLibrary->slotLoadLocationToPlayer(path, group, play);
                }
            });
}

} // namespace plugins
} // namespace mixxx

#include "moc_pluginremotelibraryfeature.cpp"
