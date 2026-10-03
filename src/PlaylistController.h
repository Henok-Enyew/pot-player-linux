#pragma once

#include "PlaylistOps.h"

#include <QObject>
#include <QTimer>
#include <QVariant>

#include <optional>
#include <utility>

class MediaProber;
class MpvWidget;
class PlaylistDrawer;
class QWidget;

// Carries out the playlist drawer's requests on mpv's playlist, which stays
// the single source of truth: reordering is done with playlist-move commands
// (like drag-and-drop), so the playing entry keeps playing. Also keeps the
// queue saved between runs.
class PlaylistController : public QObject
{
    Q_OBJECT

public:
    PlaylistController(MpvWidget *mpv, PlaylistDrawer *drawer, QWidget *dialogParent);

    // Mirrors a report of mpv's "playlist" property.
    void setPlaylist(const QVariantList &playlist);

    void addFilesDialog();
    void addFolderDialog();
    // Queues the media files in `folder` and its subfolders. The folder is
    // scanned in a worker thread; the files are queued when it is done.
    void addFolder(const QString &folder);
    // Queues `entries` at playlist index `row` (-1 appends), expanding
    // folders in a worker thread first.
    void addEntries(const QStringList &entries, int row = -1);
    void savePlaylistDialog();
    bool savePlaylist(const QString &path);

    void sort(PlaylistOps::SortKey key, bool ascending);
    void reverse();
    void shuffle();
    void removeRows(QList<int> rows);
    void clear();
    void removeMissing();
    void removeDuplicates();

    // Starts saving the queue (when enabled) and, if `restore`, reopens the
    // saved one. Returns true if a queue was restored.
    bool startSession(bool restore);
    // Saves the queue now, if enabled and the session has started.
    void saveSession();

    // mpv's current playlist, with durations (where known) and file sizes.
    QList<PlaylistOps::Entry> entries();

Q_SIGNALS:
    void message(const QString &label, const QString &value = QString());

private:
    // Re-reads the playlist from mpv.
    void refresh();
    // Rebuilds the drawer and probes new entries for the last reported playlist.
    void updateDrawer();
    void applyOrder(const QList<int> &order);
    void scheduleSave();
    QList<double> durations() const;

    MpvWidget *m_mpv;
    PlaylistDrawer *m_drawer;
    QWidget *m_dialogParent;
    MediaProber *m_prober;
    QVariantList m_playlist;
    QList<PlaylistOps::Entry> m_entries;
    // A duration sort waiting for the prober to finish.
    std::optional<std::pair<PlaylistOps::SortKey, bool>> m_pendingSort;
    QTimer m_durationTimer;
    // Coalesces playlist reports, so a burst of changes rebuilds the drawer once.
    QTimer m_playlistTimer;
    QTimer m_saveTimer;
    QTimer m_positionTimer;
    bool m_sessionStarted = false;
};
