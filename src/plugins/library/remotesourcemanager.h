#pragma once

#include <QJSValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QJSEngine;

namespace mixxx {
namespace plugins {

/// Metadata for a single remote track, parsed from the JS object returned by a
/// source's search() callback.
struct RemoteTrackData {
    QString id;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    QString year;
    QString trackNumber;
    double duration = 0.0;
    int bitrate = 0;
    int sampleRate = 0;
    QString coverArtUri;
};

/// A remote music source contributed by a script plugin through
/// `mixxx.remote.addSource()`. The QJSValue callbacks run on the main thread.
/// `engine` is the script engine that owns those values; it is only valid while
/// the contributing plugin is loaded.
struct PluginRemoteSource {
    QString sourceId;
    QString pluginId;
    QString name;
    QJSEngine* engine = nullptr;
    QJSValue search;
    QJSValue resolve;
    QJSValue download;
    QJSValue isDownloaded;
    QJSValue removeDownload;
    QJSValue coverArt;
    QJSValue count;
};

/// A per-source track context-menu action registered by a plugin.
struct PluginRemoteTrackAction {
    QString actionId;
    QString pluginId;
    QString sourceId;
    QString text;
    QJSEngine* engine = nullptr;
    QJSValue callback;
};

/// Adapts a C++ handler into a JS-callable function passed to plugin
/// callbacks (e.g. source.search(query, offset, limit, cb)).
class RemoteCallbackBridge : public QObject {
    Q_OBJECT
  public:
    explicit RemoteCallbackBridge(std::function<void(const QJSValue&)> handler,
            QObject* pParent = nullptr);

    Q_INVOKABLE void invoke(const QJSValue& value);

  private:
    std::function<void(const QJSValue&)> m_handler;
    bool m_invoked = false;
};

/// Owns the remote sources contributed by script plugins and drives their JS
/// callbacks on behalf of the generic remote-library feature.
class RemoteSourceManager : public QObject {
    Q_OBJECT
  public:
    using SearchCallback = std::function<void(bool ok,
            const QList<RemoteTrackData>& tracks,
            int total,
            const QString& error)>;
    using PathCallback = std::function<void(
            bool ok, const QString& path, const QString& error)>;
    using BoolCallback = std::function<void(bool value)>;

    explicit RemoteSourceManager(QObject* pParent = nullptr);

    QString addSource(const PluginRemoteSource& source);
    void removeSource(const QString& sourceId);
    void removePluginSources(const QString& pluginId);

    bool hasSource(const QString& sourceId) const;
    QString sourceName(const QString& sourceId) const;
    QString sourcePluginId(const QString& sourceId) const;
    QStringList sourceIds() const;

    void search(const QString& sourceId,
            const QString& query,
            int offset,
            int limit,
            SearchCallback callback);
    void resolve(const QString& sourceId,
            const QString& trackId,
            PathCallback callback);
    void download(const QString& sourceId,
            const QString& trackId,
            PathCallback callback);
    void isDownloaded(const QString& sourceId,
            const QString& trackId,
            BoolCallback callback);
    void removeDownload(const QString& sourceId,
            const QString& trackId,
            BoolCallback callback);
    void coverArt(const QString& sourceId,
            const QString& coverArtUri,
            PathCallback callback);

    void reportProgress(const QString& sourceId,
            const QString& trackId,
            qint64 received,
            qint64 total);
    void removeProgress(const QString& sourceId, const QString& trackId);
    void refresh(const QString& sourceId);

    void addTrackAction(const QString& actionId,
            const PluginRemoteTrackAction& action);
    void removeTrackAction(const QString& actionId);
    bool invokeTrackAction(const QString& actionId, const QStringList& trackIds);
    QStringList trackActionIdsForSource(const QString& sourceId) const;
    const PluginRemoteTrackAction* trackAction(
            const QString& sourceId, const QString& actionId) const;

  signals:
    void sourcesChanged();
    void refreshRequested(const QString& sourceId);
    void progressChanged(const QString& sourceId,
            const QString& trackId,
            qint64 received,
            qint64 total);

  private:
    const PluginRemoteSource* findSource(const QString& sourceId) const;

    QList<PluginRemoteSource> m_sources;
    QList<PluginRemoteTrackAction> m_trackActions;
};

} // namespace plugins
} // namespace mixxx
