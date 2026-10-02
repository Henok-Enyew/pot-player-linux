#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QUrl>

class ControlBar;
class EmptyStateWidget;
class MpvWidget;
class OsdWidget;
class PlayerMenu;
class PlaylistDrawer;
class ThumbnailGenerator;
class ThumbnailPopup;
class TitleBar;

// Borderless top-level window: skin title bar, video surface with the
// playlist drawer beside it, and the control bar underneath.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openFile(const QString &pathOrUrl);
    // Plays the first file and queues the rest.
    void openFiles(const QStringList &files);
    // Opens dropped or pasted URLs: folders are expanded, subtitle files are
    // added to the video, everything else replaces the playlist.
    void openUrls(const QList<QUrl> &urls);
    void openFileDialog();
    void openFolderDialog();
    void openUrlDialog();
    void openPlaylistDialog();
    void loadSubtitle(const QString &path);
    void toggleFullScreen();
    void setAlwaysOnTop(bool onTop);
    // Resizes the window so the video shows at `scale` times its display size.
    void scaleToVideo(qreal scale);

    bool isPlaylistVisible() const;
    void setPlaylistVisible(bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupPlaylist();
    void setupThumbnails();
    void onStateUpdated(const QString &name, const QVariant &value);
    void showPropertyOsd(const QString &name, const QVariant &value);
    void showSeekOsd();
    // Resizes the window so the video area is `scale` times `videoSize`, shrunk
    // to fit the screen, keeping the window centered. Returns false if skipped.
    bool resizeToVideo(const QSize &videoSize, qreal scale);
    // Shows or hides the title bar, control bar and drawer for fullscreen.
    void updateChrome();
    // In fullscreen, reveals the control bar near the bottom edge and hides
    // it and the cursor again after a moment without mouse movement.
    void onMouseActivity(const QPoint &globalPos);
    Qt::Edges edgesAt(const QPoint &pos) const;

    MpvWidget *m_mpv = nullptr;
    EmptyStateWidget *m_emptyState = nullptr;
    OsdWidget *m_osd = nullptr;
    PlayerMenu *m_menu = nullptr;
    TitleBar *m_titleBar = nullptr;
    ControlBar *m_controlBar = nullptr;
    PlaylistDrawer *m_drawer = nullptr;
    ThumbnailGenerator *m_thumbnails = nullptr;
    ThumbnailPopup *m_thumbnailPopup = nullptr;
    QWidget *m_root = nullptr;
    QTimer m_idleTimer;
    int m_hoverSecond = -1;
    QPoint m_popupAnchor;
    bool m_playlistBeforeFullScreen = false;
    bool m_wasFullScreen = false;
};
