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

// Media files in `folder` and its subfolders, in natural order
// ("Episode 2" before "Episode 10"), folders after the files beside them.
QStringList mediaFilesInFolder(const QString &folder);

} // namespace MediaFiles
