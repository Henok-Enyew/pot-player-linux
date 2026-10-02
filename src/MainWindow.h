#pragma once

#include <QMainWindow>

class MpvWidget;

// Borderless top-level window hosting the video surface.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openFile(const QString &pathOrUrl);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void toggleFullScreen();
    Qt::Edges edgesAt(const QPoint &pos) const;

    MpvWidget *m_mpv = nullptr;
};
