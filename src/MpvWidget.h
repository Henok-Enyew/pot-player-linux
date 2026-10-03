#pragma once

#include <QOpenGLWidget>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QVariant>

struct mpv_handle;
struct mpv_render_context;

// An OpenGL surface that renders video through libmpv's render API.
class MpvWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    // `subtitles` are added once the file has loaded.
    void loadFile(const QString &pathOrUrl, const QStringList &subtitles = {});
    // Replaces the playlist: plays the first file and queues the rest.
    void loadFiles(const QStringList &files, const QStringList &subtitles = {});
    // Replaces the playlist with the entries of a playlist file (.m3u, .pls, ...).
    void loadPlaylist(const QString &path);
    // Replaces the playlist with `files` without starting playback; Play
    // starts at entry `current`. With `resumeAt` >= 0, entry `current` is
    // opened paused at `resumeAt` seconds instead.
    void restorePlaylist(const QStringList &files, int current, double resumeAt = -1);
    // Queues files at playlist index `row` (-1 appends). Starts playback if idle.
    void insertFiles(const QStringList &files, int row = -1);
    // Adds an external subtitle file to the current file and selects it.
    void addSubtitle(const QString &path);
    void adjustVolume(double delta);

    // Transport controls. Unlike the raw mpv commands these also work after
    // stop(): the playlist is kept, and playing resumes from the last entry.
    void play();
    void pause();
    void togglePause();
    // Stops playback and blanks the video surface, keeping the playlist.
    void stop();
    void playlistNext();
    void playlistPrev();
    // True while nothing is loaded (startup, after stop() or an empty playlist).
    bool isIdle() const;
    // True if the loaded file has audio but no video, apart from cover art.
    // Known from fileLoaded() on.
    bool isAudioOnly() const { return m_audioOnly; }
    // The entry that is playing, or played last before a stop; -1 if none.
    int lastPlaylistPos() const { return m_lastPlaylistPos; }

    // Runs an mpv command asynchronously, e.g. {"seek", "5", "relative"}.
    void command(const QStringList &args);

    // Reads a property synchronously; maps and arrays become QVariantMap/QVariantList.
    QVariant mpvProperty(const QString &name) const;
    QString mpvPropertyString(const QString &name) const;
    // Sets a property asynchronously from its string form, e.g. ("speed", "1.5").
    void setMpvProperty(const QString &name, const QString &value);

    // The OpenGL renderer the video is drawn with, e.g. "Mesa Intel(R) UHD
    // Graphics 620 (KBL GT2)"; empty until the widget is first shown.
    QString glRenderer() const { return m_glRenderer; }

    // Tracks of `type` ("video", "audio" or "sub") from mpv's track-list.
    QList<QVariantMap> tracks(const QString &type) const;
    // Human-readable track name, e.g. "#2: Commentary [jpn] (aac)".
    static QString trackLabel(const QVariantMap &track);

    static bool isSubtitleFile(const QString &path);
    // A QFileDialog name filter matching subtitle files.
    static QString subtitleFileFilter();

Q_SIGNALS:
    void titleChanged(const QString &title);
    // Emitted when an observed property changes after its initial value is known.
    // Use this for notifications such as the OSD.
    void propertyChanged(const QString &name, const QVariant &value);
    // Emitted for every report of an observed property, including the initial
    // one; the value is invalid while the property is unavailable. Use this for
    // widgets that mirror player state.
    void propertyUpdated(const QString &name, const QVariant &value);
    // Emitted as mpv starts opening a playlist entry.
    void fileStarted();
    // Emitted once the entry's tracks are known (see isAudioOnly()).
    void fileLoaded();
    // Emitted once playback resumes after a user seek.
    void seeked();
    // Emitted once per file, when the video's display size is first known.
    // Not emitted for audio files, whose cover art or visualization is no
    // reason to resize the window.
    void videoSizeKnown(const QSize &size);

protected:
    void initializeGL() override;
    void paintGL() override;

private Q_SLOTS:
    void processMpvEvents();
    void onRenderUpdate();

private:
    static void onMpvWakeup(void *ctx);
    static void onMpvRenderUpdate(void *ctx);
    // Runs `commands` now, or once the render context exists.
    void runOrDefer(const QList<QStringList> &commands);
    // Plays playlist entry `index`, clamped to the playlist. Returns false if it is empty.
    bool playIndex(int index);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_renderCtx = nullptr;
    // Files requested before the GL context existed; loading them earlier
    // would make mpv's video output fail to initialize.
    // Commands that start playback, deferred until the render context exists.
    QList<QStringList> m_pendingLoads;
    QStringList m_pendingSubtitles;
    QString m_glRenderer;
    QSet<QString> m_initializedProperties;
    QSet<QString> m_stateProperties;
    bool m_fileLoaded = false;
    bool m_seeking = false;
    bool m_awaitingVideoSize = false;
    bool m_audioOnly = false;
    // The "start" option was set for a resumed entry and must not apply to later files.
    bool m_resetStart = false;
    // Mirrors idle-active for painting, which must not block on mpv.
    bool m_idle = true;
    // Playlist entry that played last; mpv forgets it on stop.
    int m_lastPlaylistPos = -1;
};
