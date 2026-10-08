#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QModelIndex>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "library/trackmodel.h"
#include "plugins/library/remotesourcemanager.h"
#include "track/track_decl.h"

class QMimeData;

namespace mixxx {
namespace plugins {

class PluginRemoteTrackModel : public QAbstractTableModel, public TrackModel {
    Q_OBJECT
  public:
    enum Column {
        ColumnTitle = 0,
        ColumnArtist,
        ColumnAlbum,
        ColumnGenre,
        ColumnYear,
        ColumnDuration,
        ColumnBitrate,
        ColumnDownloaded,
        NumColumns,
    };

    PluginRemoteTrackModel(RemoteSourceManager* pManager,
            QString sourceId,
            QObject* pParent = nullptr);

    /// Re-runs the current query against the source.
    void refresh();

    /// True if `location` belongs to a remote library source.
    static bool isRemoteLocation(const QString& location);
    static QString makeLocation(const QString& sourceId, const QString& trackId);
    static bool parseLocation(const QString& location,
            QString* pSourceId,
            QString* pTrackId);

    /// Track ids of the selected indices (used by the context menu).
    QStringList trackIdsForIndices(const QModelIndexList& indices) const;

    /// Looks up the metadata of a track by its source track id. Returns false
    /// if the track is not currently loaded in the model.
    bool trackData(const QString& trackId, RemoteTrackData* pData) const;

    /// Appends the source's track actions (including download/remove download)
    /// to a context menu.
    void addContextMenuActions(QMenu* pMenu, const QModelIndexList& indices);

    // TrackModel
    TrackPointer getTrack(const QModelIndex& index) const override;
    TrackPointer getTrackByRef(const TrackRef& trackRef) const override;
    Capabilities getCapabilities() const override;
    QString getTrackLocation(const QModelIndex& index) const override;
    TrackId getTrackId(const QModelIndex& index) const override;
    QUrl getTrackUrl(const QModelIndex& index) const override;
    CoverInfo getCoverInfo(const QModelIndex& index) const override;
    const QVector<int> getTrackRows(TrackId trackId) const override;
    void search(const QString& searchText) override;
    const QString currentSearch() const override;
    bool isColumnInternal(int column) override;
    bool isColumnHiddenByDefault(int column) override;
    const QList<int>& searchColumns() const override;
    SortColumnId sortColumnIdFromColumnIndex(int index) const override;
    int columnIndexFromSortColumnId(SortColumnId sortColumn) const override;
    QString modelKey(bool noSearch) const override;
    QString getModelSetting(const QString& name) override;
    bool setModelSetting(const QString& name, const QVariant& value) override;
    bool updateTrackGenre(Track* pTrack, const QString& genre) const override;
#if defined(__EXTRA_METADATA__)
    bool updateTrackMood(Track* pTrack, const QString& mood) const override;
#endif // __EXTRA_METADATA__

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
            Qt::Orientation orientation,
            int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    QStringList mimeTypes() const override;

  private slots:
    void onProgressChanged(const QString& sourceId,
            const QString& trackId,
            qint64 received,
            qint64 total);

  private:
    void runSearch();
    TrackPointer buildTrack(int row) const;
    const RemoteTrackData* trackDataAt(int row) const;
    void emitRowChanged(const QString& trackId);

    /// Starts (once) an asynchronous fetch of a track's cover art and caches
    /// the resulting CoverInfo. Safe to call repeatedly (e.g. from getCoverInfo
    /// during painting).
    void ensureCoverArt(const QString& trackId, const QString& coverArtUri) const;
    void onCoverArtResolved(const QString& trackId,
            const QString& coverArtUri,
            bool ok,
            const QString& path);
    CoverInfo coverInfoForTrack(const QString& trackId) const;

    QPointer<RemoteSourceManager> m_pManager;
    const QString m_sourceId;
    QList<RemoteTrackData> m_tracks;
    QHash<QString, int> m_rowByTrackId;
    QString m_search;
    QList<int> m_searchColumns;
    mutable QHash<int, TrackPointer> m_trackCache;
    QSet<QString> m_downloaded;
    QSet<QString> m_downloading;
    /// Cover art resolved to a local cache file, keyed by track id.
    mutable QHash<QString, CoverInfo> m_coverInfoByTrackId;
    /// Track ids with a cover-art fetch currently in flight.
    mutable QSet<QString> m_coverFetchInFlight;
    /// Per-model view state (column widths, header state) kept in memory; the
    /// remote model has no SQL database to persist it in.
    QHash<QString, QVariant> m_modelSettings;
    /// Guards against stale async search results arriving out of order.
    int m_searchGeneration = 0;
};

} // namespace plugins
} // namespace mixxx
