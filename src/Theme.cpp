#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

void applyDarkSkin(QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1B, 0x1B, 0x1B));
    palette.setColor(QPalette::WindowText, QColor(0xD6, 0xD6, 0xD6));
    palette.setColor(QPalette::Base, QColor(0x18, 0x18, 0x18));
    palette.setColor(QPalette::AlternateBase, QColor(0x22, 0x22, 0x22));
    palette.setColor(QPalette::Text, QColor(0xD6, 0xD6, 0xD6));
    palette.setColor(QPalette::Button, QColor(0x2A, 0x2A, 0x2A));
    palette.setColor(QPalette::ButtonText, QColor(0xD6, 0xD6, 0xD6));
    palette.setColor(QPalette::Highlight, QColor(0x3A, 0x3A, 0x3A));
    palette.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    palette.setColor(QPalette::ToolTipBase, QColor(0x26, 0x26, 0x26));
    palette.setColor(QPalette::ToolTipText, QColor(0xD6, 0xD6, 0xD6));
    palette.setColor(QPalette::PlaceholderText, QColor(0x8A, 0x8A, 0x8A));
    palette.setColor(QPalette::Link, QColor(0xFF, 0xB4, 0x1E));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(0x5A, 0x5A, 0x5A));
    QApplication::setPalette(palette);

    QFile qss(QStringLiteral(":/skin/potplayer-dark.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}
