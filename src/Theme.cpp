#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

void applyDarkSkin(QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, Theme::Surface);
    palette.setColor(QPalette::WindowText, Theme::TextPrimary);
    palette.setColor(QPalette::Base, Theme::Surface);
    palette.setColor(QPalette::AlternateBase, Theme::Panel);
    palette.setColor(QPalette::Text, Theme::TextPrimary);
    palette.setColor(QPalette::Button, Theme::Raised);
    palette.setColor(QPalette::ButtonText, Theme::TextPrimary);
    palette.setColor(QPalette::Highlight, Theme::Accent);
    palette.setColor(QPalette::HighlightedText, Theme::Surface);
    palette.setColor(QPalette::ToolTipBase, Theme::Panel);
    palette.setColor(QPalette::ToolTipText, Theme::TextPrimary);
    palette.setColor(QPalette::PlaceholderText, Theme::TextDim);
    palette.setColor(QPalette::Link, Theme::Accent);
    palette.setColor(QPalette::LinkVisited, Theme::AccentHover);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, Theme::TextDim);
    QApplication::setPalette(palette);

    QFile qss(QStringLiteral(":/skin/top-player.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}
