#pragma once

#include <QIcon>
#include <QPixmap>

// Flat skin icons drawn with QPainter, so the skin needs no image plugins.
enum class IconType {
    Open, Play, Pause, Stop, Previous, Next, Playlist, Volume, Muted, Fullscreen,
    Minimize, Maximize, Restore, Close, Add, Remove, Clear, Folder, Url, Shuffle, Sort, More, Search,
    Expand, Collapse, Repeat, RepeatOne, AspectFit, AspectWide, AspectOriginal,
};

// Checkable buttons show the icon in the accent color while checked.
QIcon skinIcon(IconType type);

// The application logo (same artwork as the desktop icon), `size` logical
// pixels square, rendered for `devicePixelRatio`.
QPixmap appLogo(int size, qreal devicePixelRatio);
