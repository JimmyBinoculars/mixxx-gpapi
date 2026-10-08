#pragma once

#include <QDrag>
#include <QDropEvent>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QUrl>
#include <functional>

#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "util/fileinfo.h"
#include "widget/trackdroptarget.h"

class DragAndDropHelper final {
  public:
    DragAndDropHelper() = delete;

    /// Predicate that reports whether a location refers to a remote track that
    /// has not necessarily been downloaded yet (e.g. "navidrome://..."). Used to
    /// let remote tracks be dropped even though no local file exists. Set by the
    /// plugin subsystem; empty when no plugin provides remote sources.
    using RemoteLocationDetector = std::function<bool(const QString&)>;
    static void setRemoteLocationDetector(RemoteLocationDetector detector);
    static bool isRemoteLocation(const QString& location);

    static QList<mixxx::FileInfo> supportedTracksFromUrls(
            const QList<QUrl>& urls,
            bool firstOnly,
            bool acceptPlaylists,
            bool acceptRemote = false);

    static bool allowDeckCloneAttempt(
            const QDropEvent& event,
            const QString& group);

    static bool dragEnterAccept(
            const QMimeData& mimeData,
            const QString& sourceIdentifier,
            bool firstOnly,
            bool acceptPlaylists,
            bool acceptRemote = false);

    static QDrag* dragTrack(
            TrackPointer pTrack,
            QWidget* pDragSource,
            const QString& sourceIdentifier);

    static QDrag* dragTrackLocations(
            const QList<QString>& locations,
            QWidget* pDragSource,
            const QString& sourceIdentifier);

    static void handleTrackDragEnterEvent(
            QDragEnterEvent* pEvent,
            const QString& group,
            UserSettingsPointer pConfig);

    static void handleTrackDropEvent(
            QDropEvent* pEvent,
            TrackDropTarget& target,
            const QString& group,
            UserSettingsPointer pConfig);

    static void mousePressed(QMouseEvent* pEvent);

    static bool mouseMoveInitiatesDrag(QMouseEvent* pEvent);
};
