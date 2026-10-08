#include "plugins/library/remotesourcemanager.h"

#include <QJSEngine>
#include <algorithm>
#include <utility>

#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginRemoteLibrary");

QString jsString(const QJSValue& object, const char* key) {
    const QJSValue value = object.property(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        return QString();
    }
    return value.toString();
}

double jsNumber(const QJSValue& object, const char* key) {
    const QJSValue value = object.property(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        return 0.0;
    }
    return value.toNumber();
}

RemoteTrackData parseTrack(const QJSValue& value) {
    RemoteTrackData track;
    track.id = jsString(value, "id");
    track.title = jsString(value, "title");
    track.artist = jsString(value, "artist");
    track.album = jsString(value, "album");
    track.albumArtist = jsString(value, "albumArtist");
    track.genre = jsString(value, "genre");
    track.year = jsString(value, "year");
    track.trackNumber = jsString(value, "trackNumber");
    track.duration = jsNumber(value, "duration");
    track.bitrate = static_cast<int>(jsNumber(value, "bitrate"));
    track.sampleRate = static_cast<int>(jsNumber(value, "sampleRate"));
    track.coverArtUri = jsString(value, "coverArtUri");
    return track;
}

/// Builds a JS function `function(result) { obj.invoke(result); }` bound to a
/// C++ handler, so async plugin callbacks can be fulfilled from C++.
QJSValue makeJsCallback(QJSEngine* pEngine,
        std::function<void(const QJSValue&)> handler,
        QObject* pParent) {
    if (pEngine == nullptr) {
        return QJSValue();
    }
    auto* pBridge = new RemoteCallbackBridge(std::move(handler), pParent);
    QJSEngine::setObjectOwnership(pBridge, QJSEngine::CppOwnership);
    const QJSValue factory = pEngine->evaluate(
            QStringLiteral("(function(obj){ return function(result){ "
                           "obj.invoke(result); }; })"),
            QStringLiteral("<mixxx-remote-callback>"));
    if (!factory.isCallable()) {
        delete pBridge;
        return QJSValue();
    }
    return factory.call({pEngine->newQObject(pBridge)});
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RemoteCallbackBridge
// ---------------------------------------------------------------------------

RemoteCallbackBridge::RemoteCallbackBridge(
        std::function<void(const QJSValue&)> handler, QObject* pParent)
        : QObject(pParent),
          m_handler(std::move(handler)) {
}

void RemoteCallbackBridge::invoke(const QJSValue& value) {
    if (m_invoked) {
        return;
    }
    m_invoked = true;
    if (m_handler) {
        m_handler(value);
    }
    deleteLater();
}

// ---------------------------------------------------------------------------
// RemoteSourceManager
// ---------------------------------------------------------------------------

RemoteSourceManager::RemoteSourceManager(QObject* pParent)
        : QObject(pParent) {
}

QString RemoteSourceManager::addSource(const PluginRemoteSource& source) {
    if (source.sourceId.isEmpty()) {
        return QString();
    }
    // Replace an existing registration with the same id (hot reload).
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources[i].sourceId == source.sourceId) {
            m_sources[i] = source;
            emit sourcesChanged();
            return source.sourceId;
        }
    }
    m_sources.append(source);
    emit sourcesChanged();
    return source.sourceId;
}

void RemoteSourceManager::removeSource(const QString& sourceId) {
    const int before = m_sources.size();
    m_sources.erase(std::remove_if(m_sources.begin(),
                            m_sources.end(),
                            [&sourceId](const PluginRemoteSource& source) {
                                return source.sourceId == sourceId;
                            }),
            m_sources.end());
    m_trackActions.erase(std::remove_if(m_trackActions.begin(),
                                 m_trackActions.end(),
                                 [&sourceId](const PluginRemoteTrackAction& action) {
                                     return action.sourceId == sourceId;
                                 }),
            m_trackActions.end());
    if (m_sources.size() != before) {
        emit sourcesChanged();
    }
}

void RemoteSourceManager::removePluginSources(const QString& pluginId) {
    const int before = m_sources.size();
    m_sources.erase(std::remove_if(m_sources.begin(),
                            m_sources.end(),
                            [&pluginId](const PluginRemoteSource& source) {
                                return source.pluginId == pluginId;
                            }),
            m_sources.end());
    m_trackActions.erase(std::remove_if(m_trackActions.begin(),
                                 m_trackActions.end(),
                                 [&pluginId](const PluginRemoteTrackAction& action) {
                                     return action.pluginId == pluginId;
                                 }),
            m_trackActions.end());
    if (m_sources.size() != before) {
        emit sourcesChanged();
    }
}

bool RemoteSourceManager::hasSource(const QString& sourceId) const {
    return findSource(sourceId) != nullptr;
}

QString RemoteSourceManager::sourceName(const QString& sourceId) const {
    const PluginRemoteSource* pSource = findSource(sourceId);
    return pSource != nullptr ? pSource->name : QString();
}

QString RemoteSourceManager::sourcePluginId(const QString& sourceId) const {
    const PluginRemoteSource* pSource = findSource(sourceId);
    return pSource != nullptr ? pSource->pluginId : QString();
}

QStringList RemoteSourceManager::sourceIds() const {
    QStringList ids;
    for (const PluginRemoteSource& source : m_sources) {
        ids.append(source.sourceId);
    }
    return ids;
}

const PluginRemoteSource* RemoteSourceManager::findSource(
        const QString& sourceId) const {
    for (const PluginRemoteSource& source : m_sources) {
        if (source.sourceId == sourceId) {
            return &source;
        }
    }
    return nullptr;
}

void RemoteSourceManager::search(const QString& sourceId,
        const QString& query,
        int offset,
        int limit,
        SearchCallback callback) {
    const PluginRemoteSource* pSource = findSource(sourceId);
    if (pSource == nullptr || !pSource->search.isCallable()) {
        if (callback) {
            callback(false, {}, 0, QStringLiteral("source has no search()"));
        }
        return;
    }
    const QJSValue search = pSource->search;
    const QJSValue cb = makeJsCallback(pSource->engine,
            [callback](const QJSValue& result) {
                if (!callback) {
                    return;
                }
                const bool ok = result.property(QStringLiteral("ok")).toBool();
                QList<RemoteTrackData> tracks;
                const QJSValue tracksValue = result.property(QStringLiteral("tracks"));
                if (tracksValue.isArray()) {
                    const int count = tracksValue.property(QStringLiteral("length")).toInt();
                    tracks.reserve(count);
                    for (int i = 0; i < count; ++i) {
                        const RemoteTrackData track =
                                parseTrack(tracksValue.property(i));
                        if (!track.id.isEmpty()) {
                            tracks.append(track);
                        }
                    }
                }
                const int total = static_cast<int>(
                        jsNumber(result, "total"));
                callback(ok,
                        tracks,
                        total,
                        jsString(result, "error"));
            },
            this);
    search.call({QJSValue(query), offset, limit, cb});
}

void RemoteSourceManager::resolve(const QString& sourceId,
        const QString& trackId,
        PathCallback callback) {
    const PluginRemoteSource* pSource = findSource(sourceId);
    if (pSource == nullptr || !pSource->resolve.isCallable()) {
        if (callback) {
            callback(false, QString(), QStringLiteral("source has no resolve()"));
        }
        return;
    }
    const QJSValue resolve = pSource->resolve;
    const QJSValue cb = makeJsCallback(pSource->engine,
            [callback](const QJSValue& result) {
                if (!callback) {
                    return;
                }
                const bool ok = result.property(QStringLiteral("ok")).toBool();
                callback(ok, jsString(result, "path"), jsString(result, "error"));
            },
            this);
    resolve.call({trackId, cb});
}

void RemoteSourceManager::download(const QString& sourceId,
        const QString& trackId,
        PathCallback callback) {
    const PluginRemoteSource* pSource = findSource(sourceId);
    if (pSource == nullptr || !pSource->download.isCallable()) {
        // Fall back to resolve() so sources that only implement resolve still
        // support predownload.
        resolve(sourceId, trackId, std::move(callback));
        return;
    }
    const QJSValue download = pSource->download;
    const QJSValue cb = makeJsCallback(pSource->engine,
            [callback](const QJSValue& result) {
                if (!callback) {
                    return;
                }
                const bool ok = result.property(QStringLiteral("ok")).toBool();
                callback(ok, jsString(result, "path"), jsString(result, "error"));
            },
            this);
    download.call({trackId, cb});
}

void RemoteSourceManager::isDownloaded(const QString& sourceId,
        const QString& trackId,
        BoolCallback callback) {
    const PluginRemoteSource* pSource = findSource(sourceId);
    if (pSource == nullptr || !pSource->isDownloaded.isCallable()) {
        if (callback) {
            callback(false);
        }
        return;
    }
    const QJSValue isDownloaded = pSource->isDownloaded;
    const QJSValue cb = makeJsCallback(pSource->engine,
            [callback](const QJSValue& result) {
                if (!callback) {
                    return;
                }
                callback(result.property(QStringLiteral("downloaded")).toBool());
            },
            this);
    isDownloaded.call({trackId, cb});
}

void RemoteSourceManager::removeDownload(const QString& sourceId,
        const QString& trackId,
        BoolCallback callback) {
    const PluginRemoteSource* pSource = findSource(sourceId);
    if (pSource == nullptr || !pSource->removeDownload.isCallable()) {
        if (callback) {
            callback(false);
        }
        return;
    }
    const QJSValue removeDownload = pSource->removeDownload;
    const QJSValue cb = makeJsCallback(pSource->engine,
            [callback](const QJSValue& result) {
                if (!callback) {
                    return;
                }
                callback(result.property(QStringLiteral("ok")).toBool());
            },
            this);
    removeDownload.call({trackId, cb});
}

void RemoteSourceManager::coverArt(const QString& sourceId,
        const QString& coverArtUri,
        PathCallback callback) {
    const PluginRemoteSource* pSource = findSource(sourceId);
    if (pSource == nullptr || !pSource->coverArt.isCallable()) {
        if (callback) {
            callback(false, QString(), QStringLiteral("source has no coverArt()"));
        }
        return;
    }
    const QJSValue coverArt = pSource->coverArt;
    const QJSValue cb = makeJsCallback(pSource->engine,
            [callback](const QJSValue& result) {
                if (!callback) {
                    return;
                }
                const bool ok = result.property(QStringLiteral("ok")).toBool();
                callback(ok, jsString(result, "path"), jsString(result, "error"));
            },
            this);
    coverArt.call({coverArtUri, cb});
}

void RemoteSourceManager::reportProgress(const QString& sourceId,
        const QString& trackId,
        qint64 received,
        qint64 total) {
    emit progressChanged(sourceId, trackId, received, total);
}

void RemoteSourceManager::removeProgress(
        const QString& sourceId, const QString& trackId) {
    emit progressChanged(sourceId, trackId, -1, -1);
}

void RemoteSourceManager::refresh(const QString& sourceId) {
    emit refreshRequested(sourceId);
}

void RemoteSourceManager::addTrackAction(
        const QString& actionId, const PluginRemoteTrackAction& action) {
    for (int i = 0; i < m_trackActions.size(); ++i) {
        if (m_trackActions[i].actionId == actionId) {
            m_trackActions[i] = action;
            return;
        }
    }
    m_trackActions.append(action);
}

void RemoteSourceManager::removeTrackAction(const QString& actionId) {
    m_trackActions.erase(std::remove_if(m_trackActions.begin(),
                                 m_trackActions.end(),
                                 [&actionId](const PluginRemoteTrackAction& action) {
                                     return action.actionId == actionId;
                                 }),
            m_trackActions.end());
}

bool RemoteSourceManager::invokeTrackAction(
        const QString& actionId, const QStringList& trackIds) {
    for (const PluginRemoteTrackAction& action : m_trackActions) {
        if (action.actionId != actionId) {
            continue;
        }
        if (!action.callback.isCallable() || action.engine == nullptr) {
            return false;
        }
        QJSValue array = action.engine->newArray(
                static_cast<uint>(trackIds.size()));
        for (int i = 0; i < trackIds.size(); ++i) {
            array.setProperty(i, trackIds[i]);
        }
        action.callback.call({array});
        return true;
    }
    return false;
}

QStringList RemoteSourceManager::trackActionIdsForSource(
        const QString& sourceId) const {
    QStringList ids;
    for (const PluginRemoteTrackAction& action : m_trackActions) {
        if (action.sourceId == sourceId) {
            ids.append(action.actionId);
        }
    }
    return ids;
}

const PluginRemoteTrackAction* RemoteSourceManager::trackAction(
        const QString& sourceId, const QString& actionId) const {
    for (const PluginRemoteTrackAction& action : m_trackActions) {
        if (action.sourceId == sourceId && action.actionId == actionId) {
            return &action;
        }
    }
    return nullptr;
}

} // namespace plugins
} // namespace mixxx

#include "moc_remotesourcemanager.cpp"
