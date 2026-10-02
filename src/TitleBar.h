#pragma once

#include <QFrame>

class QLabel;
class QToolButton;

// Slim skin title bar for the frameless window: title plus window buttons.
// Dragging it moves the window (the press falls through to MainWindow).
class TitleBar : public QFrame
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *window);

    void setTitle(const QString &title);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateMaximizeButton();
    void updateElidedTitle();

    QWidget *m_window;
    QLabel *m_title;
    QString m_fullTitle;
    QToolButton *m_maximizeButton;
};
