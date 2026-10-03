#include "PlaylistController.h"
#include "MediaFiles.h"
#include "MediaProber.h"
#include "MpvWidget.h"
#include "PlaylistDrawer.h"
#include "PlaylistSession.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

#include <algorithm>
#include <functional>
#include <numeric>

namespace {

// Duration labels are refreshed in batches while the prober works through a folder.
constexpr int kDurationRefreshMs = 150;
// The drawer follows mpv's playlist this long after its last change.
constexpr int kPlaylistRefreshMs = 30;
constexpr int kSaveDelayMs = 1000;
// While playing, the position is saved this often in case the player is killed.
constexpr int kPositionSaveMs = 30000;

} // namespace

PlaylistController::PlaylistController(MpvWidget *mpv, PlaylistDrawer *drawer, QWidget *dialogParent)
    : QObject(dialogParent)
    , m_mpv(mpv)
    , m_drawer(drawer)
    , m_dialogParent(dialogParent)
    , m_prober(new MediaProber(this))
{
    using PlaylistOps::SortKey;
    connect(m_drawer, &PlaylistDrawer::playRequested, this, [this](int index) {
        m_mpv->command({QStringLiteral("playlist-play-index"), QString::number(index)});
    });
    connect(m_drawer, &PlaylistDrawer::moveRequested, this, [this](int from, int to) {
        m_mpv->command({QStringLiteral("playlist-move"), QString::number(from), QString::number(to)});
    });
    connect(m_drawer, &PlaylistDrawer::removeRequested, this, &PlaylistController::removeRows);
    connect(m_drawer, &PlaylistDrawer::filesDropped, this, &PlaylistController::addEntries);
    connect(m_drawer, &PlaylistDrawer::addRequested, this, &PlaylistController::addFilesDialog);
    connect(m_drawer, &PlaylistDrawer::addFolderRequested, this, &PlaylistController::addFolderDialog);
    connect(m_drawer, &PlaylistDrawer::clearRequested, this, &PlaylistController::clear);
    connect(m_drawer, &PlaylistDrawer::sortRequested, this, &PlaylistController::sort);
    connect(m_drawer, &PlaylistDrawer::reverseRequested, this, &PlaylistController::reverse);
    connect(m_drawer, &PlaylistDrawer::shuffleRequested, this, &PlaylistController::shuffle);
    connect(m_drawer, &PlaylistDrawer::removeMissingRequested, this, &PlaylistController::removeMissing);
    connect(m_drawer, &PlaylistDrawer::removeDuplicatesRequested, this, &PlaylistController::removeDuplicates);
    connect(m_drawer, &PlaylistDrawer::savePlaylistRequested, this, &PlaylistController::savePlaylistDialog);

    m_playlistTimer.setSingleShot(true);
    m_playlistTimer.setInterval(kPlaylistRefreshMs);
    connect(&m_playlistTimer, &QTimer::timeout, this, &PlaylistController::updateDrawer);

    m_durationTimer.setSingleShot(true);
    m_durationTimer.setInterval(kDurationRefreshMs);
    connect(&m_durationTimer, &QTimer::timeout, this, [this] { m_drawer->setDurations(durations()); });
    connect(m_prober, &MediaProber::durationKnown, this, [this] {
        if (!m_durationTimer.isActive())
            m_durationTimer.start();
        scheduleSave();
    });
    connect(m_prober, &MediaProber::finished, this, [this] {
        if (const auto pending = std::exchange(m_pendingSort, std::nullopt))
            sort(pending->first, pending->second);
    });

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaylistController::saveSession);
    m_positionTimer.setInterval(kPositionSaveMs);
    connect(&m_positionTimer, &QTimer::timeout, this, [this] {
        if (!m_mpv->isIdle())
            saveSession();
    });
    // The playing entry changes without the playlist changing.
    connect(m_mpv, &MpvWidget::propertyUpdated, this, [this](const QString &name) {
        if (name == QLatin1String("playlist-pos"))
            scheduleSave();
    });
}

void PlaylistController::setPlaylist(const QVariantList &playlist)
{
    m_playlist = playlist;
    m_entries = PlaylistOps::fromMpv(playlist);
    if (!m_playlistTimer.isActive())
        m_playlistTimer.start();
}

void PlaylistController::updateDrawer()
{
    m_drawer->setEntries(m_playlist, durations());
    QStringList files;
    files.reserve(m_entries.size());
    for (const PlaylistOps::Entry &entry : std::as_const(m_entries))
        files.append(entry.filename);
    m_prober->probe(files);
    scheduleSave();
}

void PlaylistController::refresh()
{
    // The last report of the playlist can lag behind commands sent since, e.g.
    // by an action just before; operate on mpv's current playlist instead.
    m_playlist = m_mpv->mpvProperty(QStringLiteral("playlist")).toList();
    m_entries = PlaylistOps::fromMpv(m_playlist);
}

QList<double> PlaylistController::durations() const
{
    QList<double> result;
    result.reserve(m_entries.size());
    for (const PlaylistOps::Entry &entry : m_entries)
        result.append(m_prober->duration(entry.filename));
    return result;
}

QList<PlaylistOps::Entry> PlaylistController::entries()
{
    refresh();
    QList<PlaylistOps::Entry> result = m_entries;
    for (PlaylistOps::Entry &entry : result) {
        entry.duration = m_prober->duration(entry.filename);
        const QString local = MediaFiles::localPath(entry.filename);
        const QFileInfo info(local);
        entry.size = !local.isEmpty() && info.isFile() ? info.size() : -1;
    }
    return result;
}

void PlaylistController::addFilesDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(m_dialogParent, tr("Add to Playlist"), {},
                                                            MediaFiles::mediaFileFilter());
    if (!files.isEmpty())
        m_mpv->insertFiles(files);
}

void PlaylistController::addFolderDialog()
{
    const QString folder = QFileDialog::getExistingDirectory(m_dialogParent, tr("Add Folder to Playlist"));
    if (!folder.isEmpty())
        addFolder(folder);
}

void PlaylistController::addFolder(const QString &folder)
{
    Q_EMIT message(tr("Scanning Folder"), QFileInfo(folder).fileName());
    MediaFiles::expandFoldersAsync({folder}, this, [this, folder](const QStringList &files) {
        if (files.isEmpty()) {
            Q_EMIT message(tr("No media files in"), QFileInfo(folder).fileName());
            return;
        }
        m_mpv->insertFiles(files);
        Q_EMIT message(tr("Added to Playlist"), tr("%n file(s)", nullptr, static_cast<int>(files.size())));
    });
}

void PlaylistController::addEntries(const QStringList &entries, int row)
{
    MediaFiles::expandFoldersAsync(entries, this, [this, row](const QStringList &files) {
        if (!files.isEmpty())
            m_mpv->insertFiles(files, row);
    });
}

void PlaylistController::savePlaylistDialog()
{
    refresh();
    if (m_entries.isEmpty()) {
        Q_EMIT message(tr("Playlist is empty"));
        return;
    }
    QString filter;
    QString path = QFileDialog::getSaveFileName(m_dialogParent, tr("Save Playlist"),
                                                QDir::home().filePath(tr("Playlist") + QStringLiteral(".m3u8")),
                                                MediaFiles::playlistSaveFilter(), &filter);
    if (path.isEmpty())
        return;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("m3u") && suffix != QLatin1String("m3u8"))
        path += filter.contains(QLatin1String("*.m3u)")) ? QStringLiteral(".m3u") : QStringLiteral(".m3u8");
    savePlaylist(path);
}

bool PlaylistController::savePlaylist(const QString &path)
{
    QString error;
    if (!PlaylistOps::writeM3u(path, entries(), &error)) {
        Q_EMIT message(tr("Could not save playlist"), error);
        return false;
    }
    Q_EMIT message(tr("Playlist Saved"), QFileInfo(path).fileName());
    return true;
}

void PlaylistController::sort(PlaylistOps::SortKey key, bool ascending)
{
    refresh();
    if (key == PlaylistOps::SortKey::Duration) {
        // Wait for the durations still being read, so the order is complete.
        const bool waiting = std::any_of(m_entries.cbegin(), m_entries.cend(), [this](const PlaylistOps::Entry &entry) {
            return !m_prober->hasResult(entry.filename) && m_prober->isBusy();
        });
        if (waiting) {
            m_pendingSort = std::make_pair(key, ascending);
            Q_EMIT message(tr("Reading durations..."));
            return;
        }
    }
    applyOrder(PlaylistOps::sortOrder(entries(), key, ascending));
}

void PlaylistController::reverse()
{
    refresh();
    QList<int> order(m_entries.size());
    std::iota(order.rbegin(), order.rend(), 0);
    applyOrder(order);
}

void PlaylistController::shuffle()
{
    refresh();
    if (m_entries.size() < 2)
        return;
    m_mpv->command({QStringLiteral("playlist-shuffle")});
    Q_EMIT message(tr("Playlist Shuffled"));
}

void PlaylistController::applyOrder(const QList<int> &order)
{
    // mpv runs the commands in order, so each move sees the previous ones done.
    for (const auto &[from, to] : PlaylistOps::movesForOrder(order))
        m_mpv->command({QStringLiteral("playlist-move"), QString::number(from), QString::number(to)});
}

void PlaylistController::removeRows(QList<int> rows)
{
    // Remove from the bottom up so earlier indexes stay valid.
    std::sort(rows.begin(), rows.end(), std::greater<>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int row : std::as_const(rows))
        m_mpv->command({QStringLiteral("playlist-remove"), QString::number(row)});
}

void PlaylistController::clear()
{
    m_mpv->command({QStringLiteral("playlist-clear")});
}

void PlaylistController::removeMissing()
{
    refresh();
    const QList<int> rows = PlaylistOps::missingRows(m_entries);
    removeRows(rows);
    Q_EMIT message(tr("Removed Missing Files"), QString::number(rows.size()));
}

void PlaylistController::removeDuplicates()
{
    refresh();
    int current = -1;
    for (int row = 0; row < m_playlist.size(); ++row) {
        if (m_playlist[row].toMap().value(QStringLiteral("current")).toBool())
            current = row;
    }
    const QList<int> rows = PlaylistOps::duplicateRows(m_entries, current);
    removeRows(rows);
    Q_EMIT message(tr("Removed Duplicates"), QString::number(rows.size()));
}

bool PlaylistController::startSession(bool restore)
{
    m_sessionStarted = true;
    m_positionTimer.start();
    if (!restore || !PlaylistSession::rememberPlaylist())
        return false;
    const std::optional<PlaylistSession::State> state = PlaylistSession::load();
    if (!state || state->entries.isEmpty())
        return false;

    QStringList files;
    for (const PlaylistOps::Entry &entry : state->entries) {
        files.append(entry.filename);
        m_prober->setDuration(entry.filename, entry.duration);
    }
    if (PlaylistSession::resumePlayback())
        m_mpv->restorePlaylist(files, state->current, state->position);
    else
        m_mpv->restorePlaylist(files, state->current);
    return true;
}

void PlaylistController::saveSession()
{
    m_saveTimer.stop();
    if (!m_sessionStarted || !PlaylistSession::rememberPlaylist())
        return;

    // Read live: the last report may lag behind commands just sent.
    PlaylistSession::State state;
    state.entries = PlaylistOps::fromMpv(m_mpv->mpvProperty(QStringLiteral("playlist")).toList());
    for (PlaylistOps::Entry &entry : state.entries) {
        entry.duration = m_prober->duration(entry.filename);
        // Relative paths from the command line would not survive a different working directory.
        const QString local = MediaFiles::localPath(entry.filename);
        if (local == entry.filename && QFileInfo(local).isRelative())
            entry.filename = QFileInfo(local).absoluteFilePath();
    }
    const bool idle = m_mpv->isIdle();
    state.current = idle ? m_mpv->lastPlaylistPos() : m_mpv->mpvProperty(QStringLiteral("playlist-pos")).toInt();
    if (state.current >= state.entries.size())
        state.current = -1;
    if (!idle && !m_mpv->mpvProperty(QStringLiteral("eof-reached")).toBool())
        state.position = std::max(0.0, m_mpv->mpvProperty(QStringLiteral("time-pos")).toDouble());
    PlaylistSession::save(state);
}

void PlaylistController::scheduleSave()
{
    if (m_sessionStarted)
        m_saveTimer.start();
}
