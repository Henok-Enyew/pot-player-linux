#include "TitleBar.h"
#include "Icons.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>

TitleBar::TitleBar(QWidget *window)
    : QFrame(window)
    , m_window(window)
    , m_title(new QLabel(this))
{
    setObjectName(QStringLiteral("TitleBar"));
    m_title->setObjectName(QStringLiteral("TitleLabel"));
    // The label shows an elided copy of the title, so it must not dictate its own width.
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *appName = new QLabel(QApplication::applicationDisplayName(), this);
    appName->setObjectName(QStringLiteral("AppNameLabel"));

    auto makeButton = [this](const QString &name, IconType icon, const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setObjectName(name);
        button->setIcon(skinIcon(icon));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    QToolButton *minimize = makeButton(QStringLiteral("MinimizeButton"), IconType::Minimize, tr("Minimize"));
    m_maximizeButton = makeButton(QStringLiteral("MaximizeButton"), IconType::Maximize, tr("Maximize"));
    QToolButton *close = makeButton(QStringLiteral("CloseButton"), IconType::Close, tr("Close"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(appName);
    layout->addWidget(m_title, 1);
    layout->addWidget(minimize);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(close);

    connect(minimize, &QToolButton::clicked, m_window, &QWidget::showMinimized);
    connect(m_maximizeButton, &QToolButton::clicked, this,
            [this] { m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized(); });
    connect(close, &QToolButton::clicked, m_window, &QWidget::close);
    m_window->installEventFilter(this);
}

void TitleBar::setTitle(const QString &title)
{
    m_fullTitle = title;
    updateElidedTitle();
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::WindowStateChange)
        updateMaximizeButton();
    return QFrame::eventFilter(watched, event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

void TitleBar::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateElidedTitle();
}

void TitleBar::updateMaximizeButton()
{
    const bool maximized = m_window->isMaximized();
    m_maximizeButton->setIcon(skinIcon(maximized ? IconType::Restore : IconType::Maximize));
    m_maximizeButton->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

void TitleBar::updateElidedTitle()
{
    m_title->setText(m_title->fontMetrics().elidedText(m_fullTitle, Qt::ElideMiddle, std::max(0, m_title->width())));
    m_title->setToolTip(m_fullTitle);
}
