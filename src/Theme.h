#pragma once

#include <QColor>

class QApplication;
class QToolButton;
class QWidget;

// Top Player palette, shared by the QSS skin (resources/skin/top-player.qss)
// and the widgets that paint themselves.
namespace Theme {

inline constexpr QColor Accent{0x00, 0xD2, 0xFF};      // electric cyan
inline constexpr QColor AccentHover{0x33, 0xDC, 0xFF};
inline constexpr QColor AccentDeep{0x00, 0x90, 0xC0};
inline constexpr QColor Surface{0x12, 0x13, 0x16};     // obsidian background
inline constexpr QColor Panel{0x1A, 0x1C, 0x22};       // drawer, menus, dialogs
inline constexpr QColor Raised{0x22, 0x25, 0x2C};
inline constexpr QColor Border{0x2A, 0x2D, 0x35};
inline constexpr QColor TextPrimary{0xFF, 0xFF, 0xFF};
inline constexpr QColor TextSecondary{0xA0, 0xA5, 0xB5};
inline constexpr QColor TextDim{0x6B, 0x70, 0x80};

// "#rrggbb", for rich text.
inline QString hex(const QColor &color) { return color.name(QColor::HexRgb); }

// A text button for the bars along the bottom of the playlist and library
// ("ADD", "DEL", ...), styled as chips by the skin.
QToolButton *barButton(QWidget *parent, const QString &text, const QString &toolTip, const char *objectName);

} // namespace Theme

// Applies the Top Player skin: Fusion style with a dark palette (so standard
// dialogs match) plus the QSS skin from resources.
void applyDarkSkin(QApplication &app);
