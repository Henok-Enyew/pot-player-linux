#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QFontMetrics>
#include <QPalette>
#include <QToolButton>
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

QToolButton *Theme::barButton(QWidget *parent, const QString &text, const QString &toolTip, const char *objectName)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QString::fromLatin1(objectName));
    button->setProperty("barButton", true);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    // Set here rather than in the skin, so the size hint fits the text.
    QFont font = button->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.85);
    button->setFont(font);
    // The skin's padding is not part of a text button's size hint, and the
    // labels must stay readable however narrow the drawer is.
    button->setMinimumWidth(QFontMetrics(font).horizontalAdvance(text) + 16);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}
