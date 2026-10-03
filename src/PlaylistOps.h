#pragma once

#include <QList>
#include <QString>
#include <QVariant>

#include <utility>

// Playlist operations computed on a snapshot of mpv's playlist. They return
// row orders and indexes; PlaylistController turns those into mpv commands so
// that mpv's playlist stays the single source of truth.
namespace PlaylistOps {

struct Entry {
    QString filename; // as mpv knows it: a path or a URL
    QString title;    // from the playlist file or stream, may be empty
    double duration = -1; // seconds, negative while unknown
    qint64 size = -1;     // bytes, negative for streams and missing files
};

// Entries of mpv's "playlist" property (durations and sizes left unknown).
QList<Entry> fromMpv(const QVariantList &playlist);

// The name shown for an entry: its title, else its file name, else the URL.
QString displayName(const Entry &entry);

enum class SortKey { Name, Duration, Path, Size };

// The new order of the rows: result[i] is the current row of the entry that
// belongs at row i. Entries without a duration or size sort last.
QList<int> sortOrder(const QList<Entry> &entries, SortKey key, bool ascending = true);

// The playlist-move commands, as (from, to) pairs in mpv's semantics ("move
// `from` so that it takes the place of `to`"), that rearrange the rows into
// `order`. Rows already in place are not moved.
QList<std::pair<int, int>> movesForOrder(const QList<int> &order);

// Moving the selected rows of a playlist of `count` entries, as PotPlayer's
// move buttons do: to the top, one up, one down, or to the bottom. Returns
// the new order (as sortOrder() does) and, in `newRows`, where the selected
// rows end up. Rows already at the edge stay; the others keep their order.
enum class Shift { Top, Up, Down, Bottom };
QList<int> shiftOrder(int count, const QList<int> &rows, Shift shift, QList<int> *newRows = nullptr);

// Rows that repeat an earlier entry (same file or URL), ascending. Of a set of
// duplicates the `keepRow` entry (e.g. the one playing) is kept if it is one of them.
QList<int> duplicateRows(const QList<Entry> &entries, int keepRow = -1);

// Rows of local files that no longer exist or can't be read, ascending.
// Streams are never considered missing.
QList<int> missingRows(const QList<Entry> &entries);

// Writes an extended M3U playlist (UTF-8) with #EXTINF titles and durations.
bool writeM3u(const QString &path, const QList<Entry> &entries, QString *error = nullptr);

// The entries of an .m3u/.m3u8 or .pls playlist file: absolute paths for
// local files (relative ones resolved against the playlist's folder) and URLs
// as they are. Empty if the file can't be read.
QStringList readPlaylist(const QString &path);
// Like readPlaylist(), with each entry's title from its #EXTINF line (or a
// .pls TitleN= line); titles are empty where the file gives none.
QList<Entry> readPlaylistEntries(const QString &path);

// A playlist file read for playing: the entries that can be played, with
// local files that no longer exist left out.
struct PlaylistContents {
    QList<Entry> entries;
    int missing = 0;     // local files left out
    bool readable = true; // false if the file couldn't be read
};
PlaylistContents loadPlaylist(const QString &path);

} // namespace PlaylistOps
