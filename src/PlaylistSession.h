#pragma once

#include "PlaylistOps.h"

#include <optional>

// The queue saved between runs in ~/.config/potplayer-linux/last_playlist.json,
// and the settings that control whether it is restored.
namespace PlaylistSession {

struct State {
    QList<PlaylistOps::Entry> entries;
    int current = -1;     // entry that was playing (or last played), -1 if none
    double position = 0;  // seconds into `current`
};

// ~/.config/potplayer-linux (follows $XDG_CONFIG_HOME).
QString configDir();
QString sessionFile();

bool save(const State &state, const QString &path = sessionFile());
std::optional<State> load(const QString &path = sessionFile());

// Restore the last queue on startup (default on). Turning it off also stops
// saving it, and deletes the saved queue.
bool rememberPlaylist();
void setRememberPlaylist(bool enabled);
// Reopen the last entry where it was left, paused (default on).
bool resumePlayback();
void setResumePlayback(bool enabled);
// Width of the playlist drawer as the user last sized it, or `defaultWidth`.
int drawerWidth(int defaultWidth);
void setDrawerWidth(int width);

} // namespace PlaylistSession
