#pragma once

#include <QString>
#include <QStringList>

// File types the player opens, and helpers for the open dialogs.
namespace MediaFiles {

bool isMediaFile(const QString &path);
bool isPlaylistFile(const QString &path);

// QFileDialog name filters.
QString mediaFileFilter();
QString playlistFileFilter();
// Filter for saving playlists: only the formats the player writes (.m3u8, .m3u).
QString playlistSaveFilter();

// Media files in `folder` and its subfolders, in natural order
// ("Episode 2" before "Episode 10"), folders after the files beside them.
QStringList mediaFilesInFolder(const QString &folder);

// Natural, case-insensitive order: runs of digits compare by value. Returns
// <0, 0 or >0; only identical strings compare equal.
int naturalCompare(const QString &a, const QString &b);
inline bool naturalLess(const QString &a, const QString &b) { return naturalCompare(a, b) < 0; }

// The local file an mpv playlist entry refers to (a path or a file:// URL),
// or an empty string for streams and other URLs.
QString localPath(const QString &entry);

} // namespace MediaFiles
