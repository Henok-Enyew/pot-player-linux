#pragma once

#include <QIcon>

// Flat skin icons drawn with QPainter, so the skin needs no image plugins.
enum class IconType {
    Open, Play, Pause, Stop, Previous, Next, Playlist, Volume, Muted, Fullscreen,
    Minimize, Maximize, Restore, Close, Add, Remove, Clear,
};

QIcon skinIcon(IconType type);
