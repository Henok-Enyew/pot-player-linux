#pragma once

#include "StreamCatalog.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <optional>

class AboutDialog;
class AudioControlDialog;
class AudioController;
class AudioEffectsController;
class ControlBar;
class EmptyStateWidget;
class LiveStreamDialog;
class MpvWidget;
class OsdWidget;
class PlayerMenu;
class PlaylistController;
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
    void savePlaylistDialog();
    // The Live TV & Radio browser; created on first use.
    void openLiveStreamDialog();
    LiveStreamDialog *liveStreamDialog() const { return m_liveStreams; }
    // Plays a live stream, titled with the station's name and sent with the
    // HTTP headers it needs. If it can't be played, the channel's other
    // streams in the Live TV list are tried in turn.
    void playStream(const StreamCatalog::Station &station, bool radio);
    void queueStream(const StreamCatalog::Station &station);
    // Starts saving the queue for the next run and, if `restore`, reopens the
    // last one (as configured). Returns true if a queue was restored.
    bool startSession(bool restore);
    PlaylistController *playlist() const { return m_playlist; }
    AudioController *audio() const { return m_audio; }
    AudioEffectsController *audioEffects() const { return m_audioEffects; }
    // Audio -> Audio Control & Equalizer; created on first use.
    void openAudioControlDialog();
    AudioControlDialog *audioControlDialog() const { return m_audioControl; }
    void loadSubtitle(const QString &path);
    // Opens the OpenSubtitles search for the playing file.
    void openSubtitleDownloadDialog();
    void openSubtitleSettingsDialog();
    // Help -> About Top Player (F1).
    void showAbout();

    // Download from URL... (yt-dlp).
    void openMediaDownloaderDialog();

    // The cutter's In (A) and Out (B) points: set to the playback position,
    // shown on the seekbar, and cleared when another file opens. -1 if unset.
    void setClipIn();
    void setClipOut();
    void clearClipRange();
    double clipIn() const { return m_clipIn; }
    double clipOut() const { return m_clipOut; }
    // Tools -> Cut / Extract Media...
    void openMediaCutterDialog();
    // Opens the file manager at `path`, selecting it where supported.
    static void showInFileManager(const QString &path);
    void toggleFullScreen();
    // Leaves fullscreen for the maximized or normal geometry the window had before.
    void exitFullScreen();
    void setAlwaysOnTop(bool onTop);
    // Resizes the window so the video shows at `scale` times its display size.
    void scaleToVideo(qreal scale);

    bool isPlaylistVisible() const;
    void setPlaylistVisible(bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void startStream(const StreamCatalog::Station &station);
    void onFileFailed(const QString &path, const QString &error);
    void setupPlaylist();
    void setupThumbnails();
    void onStateUpdated(const QString &name, const QVariant &value);
    void showPropertyOsd(const QString &name, const QVariant &value);
    void showSeekOsd();
    void updateClipRange();
    void onClipExported(const QString &path);
    // Resizes the window so the video area is `scale` times `videoSize`, shrunk
    // to fit the screen, keeping the window centered. Returns false if skipped.
    bool resizeToVideo(const QSize &videoSize, qreal scale);
    // Shows or hides the title bar, control bar and drawer for fullscreen.
    void updateChrome();
    // In fullscreen, reveals the control bar near the bottom edge and hides
    // it and the cursor again after a moment without mouse movement.
    void onMouseActivity(const QPoint &globalPos);
    Qt::Edges edgesAt(const QPoint &pos) const;
    bool isOverVideo(const QPoint &globalPos) const;

    MpvWidget *m_mpv = nullptr;
    EmptyStateWidget *m_emptyState = nullptr;
    OsdWidget *m_osd = nullptr;
    PlayerMenu *m_menu = nullptr;
    TitleBar *m_titleBar = nullptr;
    ControlBar *m_controlBar = nullptr;
    PlaylistDrawer *m_drawer = nullptr;
    PlaylistController *m_playlist = nullptr;
    AudioController *m_audio = nullptr;
    ThumbnailGenerator *m_thumbnails = nullptr;
    ThumbnailPopup *m_thumbnailPopup = nullptr;
    LiveStreamDialog *m_liveStreams = nullptr;
    AudioEffectsController *m_audioEffects = nullptr;
    AudioControlDialog *m_audioControl = nullptr;
    // The live stream played last, and its channel's untried other streams.
    StreamCatalog::Station m_stream;
    bool m_streamRadio = false;
    QList<StreamCatalog::Station> m_streamFallbacks;
    QWidget *m_root = nullptr;
    QPointer<AboutDialog> m_about;
    QTimer m_idleTimer;
    // A click on the video pauses once it is clear that no double click follows.
    QTimer m_clickTimer;
    // A left press on the video that may still become a click or a window drag.
    std::optional<QPoint> m_videoPress;
    QRect m_geometryBeforeFullScreen;
    bool m_maximizedBeforeFullScreen = false;
    int m_hoverSecond = -1;
    QPoint m_popupAnchor;
    bool m_playlistBeforeFullScreen = false;
    bool m_wasFullScreen = false;
    double m_clipIn = -1;
    double m_clipOut = -1;
};
