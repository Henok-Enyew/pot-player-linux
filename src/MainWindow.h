#pragma once

#include <QMainWindow>

class MpvWidget;
class OsdWidget;
class PlayerMenu;

// Borderless top-level window hosting the video surface.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openFile(const QString &pathOrUrl);
    void openFileDialog();
    void toggleFullScreen();
    void setAlwaysOnTop(bool onTop);
    // Resizes the window to `scale` times the video's display size.
    void scaleToVideo(qreal scale);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void showPropertyOsd(const QString &name, const QVariant &value);
    void showSeekOsd();
    Qt::Edges edgesAt(const QPoint &pos) const;

    MpvWidget *m_mpv = nullptr;
    OsdWidget *m_osd = nullptr;
    PlayerMenu *m_menu = nullptr;
};
