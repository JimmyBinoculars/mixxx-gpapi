#include "plugins/library/pluginremotetrackmodel.h"

#include <QAction>
#include <QCryptographicHash>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
#include <QVariant>

#include "track/track.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginRemoteLibrary");

constexpr int kSearchLimit = 500;
const char kRemoteLocationPrefix[] = "/mixxx-remote/";

QString formatDuration(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    if (total <= 0) {
        return QString();
    }
    const int minutes = total / 60;
    const int secs = total % 60;
    return QStringLiteral("%1:%2")
            .arg(minutes)
            .arg(secs, 2, 10, QLatin1Char('0'));
}

TrackId syntheticTrackId(const QString& location) {
    // TrackId wraps a signed int, so clear the sign bit of the hash.
    int id = static_cast<int>(qHash(location) & 0x7fffffff);
    if (id == 0) {
        // TrackId 0 is reserved for invalid ids.
        id = 1;
    }
    return TrackId(QVariant(id));
}

} // anonymous namespace

PluginRemoteTrackModel::PluginRemoteTrackModel(RemoteSourceManager* pManager,
        QString sourceId,
        QObject* pParent)
        : QAbstractTableModel(pParent),
          TrackModel(QSqlDatabase(), "mixxx.db.model.remote"),
          m_pManager(pManager),
          m_sourceId(std::move(sourceId)) {
    m_searchColumns = {ColumnTitle, ColumnArtist, ColumnAlbum, ColumnGenre};
    if (m_pManager != nullptr) {
        connect(m_pManager,
                &RemoteSourceManager::progressChanged,
                this,
                &PluginRemoteTrackModel::onProgressChanged);
    }
}

bool PluginRemoteTrackModel::isRemoteLocation(const QString& location) {
    return location.startsWith(QLatin1String(kRemoteLocationPrefix));
}

QString PluginRemoteTrackModel::makeLocation(
        const QString& sourceId, const QString& trackId) {
    return QLatin1String(kRemoteLocationPrefix) +
            QString::fromLatin1(QUrl::toPercentEncoding(sourceId)) +
            QLatin1Char('/') +
            QString::fromLatin1(QUrl::toPercentEncoding(trackId));
}

bool PluginRemoteTrackModel::parseLocation(const QString& location,
        QString* pSourceId,
        QString* pTrackId) {
    if (!isRemoteLocation(location)) {
        return false;
    }
    const QString remainder =
            location.mid(static_cast<int>(sizeof(kRemoteLocationPrefix)) - 1);
    const int separator = remainder.indexOf(QLatin1Char('/'));
    if (separator < 0) {
        return false;
    }
    if (pSourceId != nullptr) {
        *pSourceId = QUrl::fromPercentEncoding(
                remainder.left(separator).toLatin1());
    }
    if (pTrackId != nullptr) {
        *pTrackId = QUrl::fromPercentEncoding(
                remainder.mid(separator + 1).toLatin1());
    }
    return true;
}

const RemoteTrackData* PluginRemoteTrackModel::trackDataAt(int row) const {
    if (row < 0 || row >= m_tracks.size()) {
        return nullptr;
    }
    return &m_tracks[row];
}

bool PluginRemoteTrackModel::trackData(
        const QString& trackId, RemoteTrackData* pData) const {
    const auto it = m_rowByTrackId.constFind(trackId);
    if (it == m_rowByTrackId.constEnd()) {
        return false;
    }
    const RemoteTrackData* pTrack = trackDataAt(it.value());
    if (pTrack == nullptr) {
        return false;
    }
    if (pData != nullptr) {
        *pData = *pTrack;
    }
    return true;
}

int PluginRemoteTrackModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_tracks.size();
}

int PluginRemoteTrackModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return NumColumns;
}

QVariant PluginRemoteTrackModel::data(const QModelIndex& index, int role) const {
    const RemoteTrackData* pData = trackDataAt(index.row());
    if (pData == nullptr) {
        return QVariant();
    }
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (index.column()) {
        case ColumnTitle:
            return pData->title;
        case ColumnArtist:
            return pData->artist;
        case ColumnAlbum:
            return pData->album;
        case ColumnGenre:
            return pData->genre;
        case ColumnYear:
            return pData->year;
        case ColumnDuration:
            return formatDuration(pData->duration);
        case ColumnBitrate:
            return pData->bitrate > 0
                    ? QStringLiteral("%1 kbps").arg(pData->bitrate)
                    : QString();
        case ColumnDownloaded:
            if (m_downloading.contains(pData->id)) {
                return tr("Downloading\u2026");
            }
            if (m_downloaded.contains(pData->id)) {
                return tr("Downloaded");
            }
            return QString();
        default:
            return QVariant();
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() == ColumnDuration) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant PluginRemoteTrackModel::headerData(
        int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case ColumnTitle:
        return tr("Title");
    case ColumnArtist:
        return tr("Artist");
    case ColumnAlbum:
        return tr("Album");
    case ColumnGenre:
        return tr("Genre");
    case ColumnYear:
        return tr("Year");
    case ColumnDuration:
        return tr("Duration");
    case ColumnBitrate:
        return tr("Bitrate");
    case ColumnDownloaded:
        return tr("Offline");
    default:
        return QVariant();
    }
}

Qt::ItemFlags PluginRemoteTrackModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QMimeData* PluginRemoteTrackModel::mimeData(const QModelIndexList& indexes) const {
    Q_UNUSED(indexes);
    // Mixxx initiates track drags explicitly in
    // WTrackTableView::mouseMoveEvent() from getTrackLocation(), so the model's
    // QAbstractItemModel drag payload is never used.
    return new QMimeData();
}

QStringList PluginRemoteTrackModel::mimeTypes() const {
    return QStringList();
}

TrackPointer PluginRemoteTrackModel::getTrack(const QModelIndex& index) const {
    const int row = index.row();
    const RemoteTrackData* pData = trackDataAt(row);
    if (pData != nullptr) {
        // Selection (and other) lookups are a good moment to make sure the
        // large cover art panel will have an image for this track.
        ensureCoverArt(pData->id, pData->coverArtUri);
    }
    const auto it = m_trackCache.constFind(row);
    if (it != m_trackCache.constEnd()) {
        return it.value();
    }
    TrackPointer pTrack = buildTrack(row);
    m_trackCache.insert(row, pTrack);
    return pTrack;
}

TrackPointer PluginRemoteTrackModel::buildTrack(int row) const {
    const RemoteTrackData* pData = trackDataAt(row);
    if (pData == nullptr) {
        return TrackPointer();
    }
    const QString location = makeLocation(m_sourceId, pData->id);
    TrackPointer pTrack = Track::newDummy(location, syntheticTrackId(location));
    pTrack->setTitle(pData->title);
    pTrack->setArtist(pData->artist);
    pTrack->setAlbum(pData->album);
    pTrack->setAlbumArtist(pData->albumArtist);
    pTrack->setYear(pData->year);
    pTrack->setTrackNumber(pData->trackNumber);
    if (pData->duration > 0.0) {
        pTrack->setDuration(pData->duration);
    }
    if (pData->bitrate > 0) {
        pTrack->setBitrate(pData->bitrate);
    }
    const CoverInfo coverInfo = coverInfoForTrack(pData->id);
    if (coverInfo.hasImage()) {
        pTrack->setCoverInfo(coverInfo);
    }
    return pTrack;
}

TrackPointer PluginRemoteTrackModel::getTrackByRef(const TrackRef& trackRef) const {
    QString sourceId;
    QString trackId;
    if (!parseLocation(trackRef.getLocation(), &sourceId, &trackId) ||
            sourceId != m_sourceId) {
        return TrackPointer();
    }
    const auto it = m_rowByTrackId.constFind(trackId);
    if (it == m_rowByTrackId.constEnd()) {
        return TrackPointer();
    }
    return getTrack(index(it.value(), ColumnTitle));
}

QString PluginRemoteTrackModel::getTrackLocation(const QModelIndex& index) const {
    const RemoteTrackData* pData = trackDataAt(index.row());
    if (pData == nullptr) {
        return QString();
    }
    return makeLocation(m_sourceId, pData->id);
}

TrackId PluginRemoteTrackModel::getTrackId(const QModelIndex& index) const {
    const QString location = getTrackLocation(index);
    if (location.isEmpty()) {
        return TrackId();
    }
    return syntheticTrackId(location);
}

QUrl PluginRemoteTrackModel::getTrackUrl(const QModelIndex& index) const {
    const QString location = getTrackLocation(index);
    if (location.isEmpty()) {
        return QUrl();
    }
    return QUrl::fromLocalFile(location);
}

CoverInfo PluginRemoteTrackModel::getCoverInfo(const QModelIndex& index) const {
    const RemoteTrackData* pData = trackDataAt(index.row());
    if (pData == nullptr || pData->coverArtUri.isEmpty()) {
        return CoverInfo();
    }
    // Kick off (or reuse) an asynchronous fetch; returns an empty CoverInfo
    // until the image has been cached locally.
    ensureCoverArt(pData->id, pData->coverArtUri);
    const auto it = m_coverInfoByTrackId.constFind(pData->id);
    return it != m_coverInfoByTrackId.constEnd() ? it.value() : CoverInfo();
}

CoverInfo PluginRemoteTrackModel::coverInfoForTrack(const QString& trackId) const {
    const auto it = m_coverInfoByTrackId.constFind(trackId);
    return it != m_coverInfoByTrackId.constEnd() ? it.value() : CoverInfo();
}

void PluginRemoteTrackModel::ensureCoverArt(
        const QString& trackId, const QString& coverArtUri) const {
    if (m_pManager == nullptr || coverArtUri.isEmpty() ||
            m_coverInfoByTrackId.contains(trackId) ||
            m_coverFetchInFlight.contains(trackId)) {
        return;
    }
    m_coverFetchInFlight.insert(trackId);
    // The model may be destroyed while the fetch is in flight.
    const QPointer<PluginRemoteTrackModel> self(
            const_cast<PluginRemoteTrackModel*>(this));
    m_pManager->coverArt(m_sourceId,
            coverArtUri,
            [self, trackId, coverArtUri](
                    bool ok, const QString& path, const QString& error) {
                Q_UNUSED(error);
                if (self != nullptr) {
                    self->onCoverArtResolved(trackId, coverArtUri, ok, path);
                }
            });
}

void PluginRemoteTrackModel::onCoverArtResolved(const QString& trackId,
        const QString& coverArtUri,
        bool ok,
        const QString& path) {
    m_coverFetchInFlight.remove(trackId);
    if (!ok || path.isEmpty()) {
        kLogger.debug() << "No cover art for" << trackId;
        return;
    }

    CoverInfo coverInfo;
    coverInfo.type = CoverInfo::FILE;
    coverInfo.source = CoverInfo::GUESSED;
    coverInfo.coverLocation = path;
    coverInfo.trackLocation = makeLocation(m_sourceId, trackId);
    // Use a stable digest derived from the cover id so the pixmap cache gets a
    // unique key without loading (and hashing) the image on the GUI thread.
    coverInfo.setImageDigest(QCryptographicHash::hash(
            coverArtUri.toUtf8(), QCryptographicHash::Md5));
    m_coverInfoByTrackId.insert(trackId, coverInfo);

    const auto rowIt = m_rowByTrackId.constFind(trackId);
    if (rowIt == m_rowByTrackId.constEnd()) {
        // The track is no longer in the current result set; the CoverInfo is
        // cached and will be applied the next time the track is built.
        return;
    }
    const int row = rowIt.value();
    TrackPointer pTrack = getTrack(index(row, ColumnTitle));
    if (pTrack != nullptr) {
        // Notifies the (large) cover art widget listening on the track.
        pTrack->setCoverInfo(coverInfo);
    }
    if (row >= 0 && row < m_tracks.size()) {
        emit dataChanged(index(row, 0), index(row, NumColumns - 1));
    }
}

const QVector<int> PluginRemoteTrackModel::getTrackRows(TrackId trackId) const {
    if (!trackId.isValid()) {
        return QVector<int>();
    }
    QVector<int> rows;
    for (int row = 0; row < m_tracks.size(); ++row) {
        if (syntheticTrackId(makeLocation(m_sourceId, m_tracks[row].id)) == trackId) {
            rows.append(row);
        }
    }
    return rows;
}

TrackModel::Capabilities PluginRemoteTrackModel::getCapabilities() const {
    return Capability::LoadToDeck;
}

void PluginRemoteTrackModel::search(const QString& searchText) {
    m_search = searchText;
    runSearch();
}

const QString PluginRemoteTrackModel::currentSearch() const {
    return m_search;
}

void PluginRemoteTrackModel::refresh() {
    runSearch();
}

void PluginRemoteTrackModel::runSearch() {
    if (m_pManager == nullptr) {
        return;
    }
    const int generation = ++m_searchGeneration;
    // The model may be destroyed while the search is in flight.
    const QPointer<PluginRemoteTrackModel> self(this);
    m_pManager->search(m_sourceId,
            m_search,
            0,
            kSearchLimit,
            [self, generation](bool ok,
                    const QList<RemoteTrackData>& tracks,
                    int total,
                    const QString& error) {
                Q_UNUSED(total);
                if (self == nullptr || generation != self->m_searchGeneration) {
                    return;
                }
                self->beginResetModel();
                self->m_tracks = tracks;
                self->m_rowByTrackId.clear();
                self->m_trackCache.clear();
                for (int i = 0; i < self->m_tracks.size(); ++i) {
                    self->m_rowByTrackId.insert(self->m_tracks[i].id, i);
                }
                self->endResetModel();
                if (!ok) {
                    kLogger.warning() << "Remote search failed for"
                                      << self->m_sourceId << ":" << error;
                }
            });
}

bool PluginRemoteTrackModel::isColumnInternal(int column) {
    Q_UNUSED(column);
    return false;
}

bool PluginRemoteTrackModel::isColumnHiddenByDefault(int column) {
    Q_UNUSED(column);
    return false;
}

const QList<int>& PluginRemoteTrackModel::searchColumns() const {
    return m_searchColumns;
}

TrackModel::SortColumnId PluginRemoteTrackModel::sortColumnIdFromColumnIndex(
        int index) const {
    Q_UNUSED(index);
    return SortColumnId::Invalid;
}

int PluginRemoteTrackModel::columnIndexFromSortColumnId(
        SortColumnId sortColumn) const {
    Q_UNUSED(sortColumn);
    return -1;
}

QString PluginRemoteTrackModel::modelKey(bool noSearch) const {
    if (noSearch) {
        return QStringLiteral("remote:") + m_sourceId;
    }
    return QStringLiteral("remote:") + m_sourceId + QLatin1Char(':') + m_search;
}

QString PluginRemoteTrackModel::getModelSetting(const QString& name) {
    return m_modelSettings.value(name).toString();
}

bool PluginRemoteTrackModel::setModelSetting(
        const QString& name, const QVariant& value) {
    m_modelSettings.insert(name, value);
    return true;
}

bool PluginRemoteTrackModel::updateTrackGenre(
        Track* pTrack, const QString& genre) const {
    Q_UNUSED(pTrack);
    Q_UNUSED(genre);
    return false;
}

#if defined(__EXTRA_METADATA__)
bool PluginRemoteTrackModel::updateTrackMood(
        Track* pTrack, const QString& mood) const {
    Q_UNUSED(pTrack);
    Q_UNUSED(mood);
    return false;
}
#endif // __EXTRA_METADATA__

QStringList PluginRemoteTrackModel::trackIdsForIndices(
        const QModelIndexList& indices) const {
    QStringList ids;
    QSet<int> rows;
    for (const QModelIndex& index : indices) {
        if (!index.isValid() || rows.contains(index.row())) {
            continue;
        }
        rows.insert(index.row());
        const RemoteTrackData* pData = trackDataAt(index.row());
        if (pData != nullptr) {
            ids.append(pData->id);
        }
    }
    return ids;
}

void PluginRemoteTrackModel::addContextMenuActions(
        QMenu* pMenu, const QModelIndexList& indices) {
    if (pMenu == nullptr || m_pManager == nullptr) {
        return;
    }
    const QStringList ids = trackIdsForIndices(indices);
    if (ids.isEmpty()) {
        return;
    }

    QList<QAction*> pluginActions;
    const QStringList actionIds =
            m_pManager->trackActionIdsForSource(m_sourceId);
    for (const QString& actionId : actionIds) {
        const PluginRemoteTrackAction* pAction =
                m_pManager->trackAction(m_sourceId, actionId);
        if (pAction == nullptr) {
            continue;
        }
        auto* pQAction = new QAction(pAction->text, pMenu);
        const QString capturedActionId = actionId;
        const QStringList capturedIds = ids;
        connect(pQAction, &QAction::triggered, pMenu, [this, capturedActionId, capturedIds]() {
            if (m_pManager != nullptr) {
                m_pManager->invokeTrackAction(capturedActionId, capturedIds);
            }
        });
        pluginActions.append(pQAction);
    }

    if (!pluginActions.isEmpty()) {
        pMenu->addSeparator();
        for (QAction* pAction : pluginActions) {
            pMenu->addAction(pAction);
        }
    }

    pMenu->addSeparator();
    auto* pDownload = new QAction(tr("Download for offline"), pMenu);
    connect(pDownload, &QAction::triggered, pMenu, [this, ids]() {
        if (m_pManager == nullptr) {
            return;
        }
        for (const QString& trackId : ids) {
            if (!m_downloading.contains(trackId)) {
                m_downloading.insert(trackId);
                emitRowChanged(trackId);
            }
            QPointer<PluginRemoteTrackModel> self(this);
            m_pManager->download(m_sourceId,
                    trackId,
                    [self, trackId](bool ok, const QString& path, const QString& error) {
                        Q_UNUSED(path);
                        if (self == nullptr) {
                            return;
                        }
                        if (!ok) {
                            kLogger.warning()
                                    << "Download failed for" << trackId << ":"
                                    << error;
                        } else {
                            // Progress signals normally set the final state; if
                            // the source did not report progress, the download
                            // callback still marks the track as available.
                            self->m_downloaded.insert(trackId);
                        }
                        self->m_downloading.remove(trackId);
                        self->emitRowChanged(trackId);
                    });
        }
    });
    pMenu->addAction(pDownload);

    bool anyDownloaded = false;
    for (const QString& trackId : ids) {
        if (m_downloaded.contains(trackId)) {
            anyDownloaded = true;
            break;
        }
    }
    if (anyDownloaded) {
        auto* pRemove = new QAction(tr("Remove download"), pMenu);
        connect(pRemove, &QAction::triggered, pMenu, [this, ids]() {
            if (m_pManager == nullptr) {
                return;
            }
            for (const QString& trackId : ids) {
                QPointer<PluginRemoteTrackModel> self(this);
                m_pManager->removeDownload(m_sourceId,
                        trackId,
                        [self, trackId](bool ok) {
                            if (self == nullptr) {
                                return;
                            }
                            if (ok) {
                                self->m_downloaded.remove(trackId);
                            }
                            self->emitRowChanged(trackId);
                        });
            }
        });
        pMenu->addAction(pRemove);
    }
}

void PluginRemoteTrackModel::emitRowChanged(const QString& trackId) {
    const auto it = m_rowByTrackId.constFind(trackId);
    if (it == m_rowByTrackId.constEnd()) {
        return;
    }
    const QModelIndex idx = index(it.value(), ColumnDownloaded);
    emit dataChanged(idx, idx);
}

void PluginRemoteTrackModel::onProgressChanged(const QString& sourceId,
        const QString& trackId,
        qint64 received,
        qint64 total) {
    Q_UNUSED(total);
    if (sourceId != m_sourceId || m_pManager == nullptr) {
        return;
    }
    if (received >= 0) {
        if (!m_downloading.contains(trackId)) {
            m_downloading.insert(trackId);
            emitRowChanged(trackId);
        }
        return;
    }
    // received < 0: the download finished (successfully or not). Clear the
    // in-progress state and ask the source whether the file is really there, so
    // a failed download is not reported as "Downloaded".
    if (m_downloading.remove(trackId)) {
        emitRowChanged(trackId);
    }
    QPointer<PluginRemoteTrackModel> self(this);
    m_pManager->isDownloaded(m_sourceId, trackId, [self, trackId](bool downloaded) {
        if (self == nullptr) {
            return;
        }
        if (downloaded) {
            self->m_downloaded.insert(trackId);
        } else {
            self->m_downloaded.remove(trackId);
        }
        self->emitRowChanged(trackId);
    });
}

} // namespace plugins
} // namespace mixxx

#include "moc_pluginremotetrackmodel.cpp"
