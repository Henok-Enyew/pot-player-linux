#include "MpvWidget.h"
#include "MpvHelpers.h"

#include <QByteArray>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPalette>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return nullptr;
    return reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name)));
}

QVariant nodeToVariant(const mpv_node *node)
{
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_FLAG:
        return node->u.flag != 0;
    case MPV_FORMAT_INT64:
        return static_cast<qlonglong>(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node->u.list->num; ++i)
            list.append(nodeToVariant(&node->u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node->u.list->num; ++i)
            map.insert(QString::fromUtf8(node->u.list->keys[i]), nodeToVariant(&node->u.list->values[i]));
        return map;
    }
    default:
        return {};
    }
}

// Player state mirrored by widgets through propertyUpdated() only.
constexpr const char *kStateProperties[] = {
    "time-pos", "duration", "playlist", "chapter-list", "path", "idle-active", "playlist-pos",
};

// Properties whose changes are also forwarded through propertyChanged().
constexpr const char *kObservedProperties[] = {
    "volume", "mute", "speed", "pause", "audio-delay", "sub-delay", "sub-scale", "sub-pos",
};

const QStringList kSubtitleExtensions{
    QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"), QStringLiteral("vtt"),
    QStringLiteral("sub"), QStringLiteral("idx"), QStringLiteral("sup"), QStringLiteral("smi"),
};

} // namespace

MpvWidget::MpvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv)
        throw std::runtime_error("could not create mpv context");

    // Rendering happens through the render API, and all input is handled by Qt.
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    // The OSD is drawn by Qt; mpv only renders subtitles.
    mpv_set_option_string(m_mpv, "osd-level", "0");
    mpv_set_option_string(m_mpv, "osd-bar", "no");
    // Like PotPlayer, pick up subtitles next to the video or in a subtitle folder.
    mpv_set_option_string(m_mpv, "sub-auto", "fuzzy");
    mpv_set_option_string(m_mpv, "sub-file-paths", "sub:subs:subtitles:Subs:Subtitles");
    mpv_set_option_string(m_mpv, "terminal", "yes");
    mpv_set_option_string(m_mpv, "msg-level", "all=warn");

    if (mpv_initialize(m_mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");

    mpv_observe_property(m_mpv, 0, "media-title", MPV_FORMAT_STRING);
    for (const char *name : kObservedProperties)
        mpv_observe_property(m_mpv, 0, name, MPV_FORMAT_NODE);
    for (const char *name : kStateProperties)
        mpv_observe_property(m_mpv, 0, name, MPV_FORMAT_NODE);
    for (const char *name : kStateProperties)
        m_stateProperties.insert(QString::fromLatin1(name));
    mpv_set_wakeup_callback(m_mpv, &MpvWidget::onMpvWakeup, this);
}

MpvWidget::~MpvWidget()
{
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    makeCurrent();
    if (m_renderCtx)
        mpv_render_context_free(m_renderCtx);
    doneCurrent();
    mpv_terminate_destroy(m_mpv);
}

void MpvWidget::loadFile(const QString &pathOrUrl, const QStringList &subtitles)
{
    loadFiles({pathOrUrl}, subtitles);
}

void MpvWidget::loadFiles(const QStringList &files, const QStringList &subtitles)
{
    if (files.isEmpty())
        return;
    m_pendingSubtitles = subtitles;
    QList<QStringList> commands{{QStringLiteral("loadfile"), files.first(), QStringLiteral("replace")}};
    for (qsizetype i = 1; i < files.size(); ++i)
        commands.append({QStringLiteral("loadfile"), files[i], QStringLiteral("append")});

    if (!m_renderCtx) {
        m_pendingLoads = commands;
        return;
    }
    for (const QStringList &cmd : std::as_const(commands))
        command(cmd);
}

void MpvWidget::loadPlaylist(const QString &path)
{
    m_pendingSubtitles.clear();
    const QStringList cmd{QStringLiteral("loadlist"), path, QStringLiteral("replace")};
    if (!m_renderCtx) {
        m_pendingLoads = {cmd};
        return;
    }
    command(cmd);
}

void MpvWidget::insertFiles(const QStringList &files, int row)
{
    if (!m_renderCtx) {
        for (const QString &file : files)
            m_pendingLoads.append({QStringLiteral("loadfile"), file, QStringLiteral("append-play")});
        return;
    }
    // Commands run in order, so each appended entry is at count + i when it is moved.
    const int count = mpvProperty(QStringLiteral("playlist-count")).toInt();
    for (qsizetype i = 0; i < files.size(); ++i) {
        command({QStringLiteral("loadfile"), files[i], QStringLiteral("append-play")});
        if (row >= 0 && row < count) {
            command({QStringLiteral("playlist-move"), QString::number(count + i),
                     QString::number(row + i)});
        }
    }
}

void MpvWidget::addSubtitle(const QString &path)
{
    command({QStringLiteral("sub-add"), path, QStringLiteral("select")});
}

void MpvWidget::adjustVolume(double delta)
{
    command({QStringLiteral("add"), QStringLiteral("volume"), QString::number(delta)});
}

bool MpvWidget::isIdle() const
{
    // Asked live: the observed value can lag behind a just-started file.
    return mpvProperty(QStringLiteral("idle-active")).toBool();
}

void MpvWidget::play()
{
    if (isIdle()) {
        // The pause flag outlives stop; clear it so the entry doesn't start paused.
        setMpvProperty(QStringLiteral("pause"), QStringLiteral("no"));
        playIndex(std::max(m_lastPlaylistPos, 0));
        return;
    }
    // With keep-open, mpv pauses on the last frame; playing again starts over.
    if (mpvProperty(QStringLiteral("eof-reached")).toBool())
        command({QStringLiteral("seek"), QStringLiteral("0"), QStringLiteral("absolute")});
    setMpvProperty(QStringLiteral("pause"), QStringLiteral("no"));
}

void MpvWidget::pause()
{
    // Pausing while idle would make the next file start paused.
    if (!isIdle())
        setMpvProperty(QStringLiteral("pause"), QStringLiteral("yes"));
}

void MpvWidget::togglePause()
{
    if (isIdle() || mpvProperty(QStringLiteral("pause")).toBool())
        play();
    else
        pause();
}

void MpvWidget::stop()
{
    // The playlist-pos notification may still be queued; remember the live entry.
    const int pos = mpvProperty(QStringLiteral("playlist-pos")).toInt();
    if (pos >= 0)
        m_lastPlaylistPos = pos;
    command({QStringLiteral("stop"), QStringLiteral("keep-playlist")});
}

void MpvWidget::playlistNext()
{
    // mpv has no current entry to step from once stopped.
    if (isIdle())
        playIndex(m_lastPlaylistPos + 1);
    else
        command({QStringLiteral("playlist-next")});
}

void MpvWidget::playlistPrev()
{
    if (isIdle())
        playIndex(std::max(m_lastPlaylistPos - 1, 0));
    else
        command({QStringLiteral("playlist-prev")});
}

bool MpvWidget::playIndex(int index)
{
    const int count = mpvProperty(QStringLiteral("playlist-count")).toInt();
    if (count <= 0)
        return false;
    command({QStringLiteral("playlist-play-index"), QString::number(std::clamp(index, 0, count - 1))});
    return true;
}

void MpvWidget::command(const QStringList &args)
{
    mpvCommandAsync(m_mpv, args);
}

QVariant MpvWidget::mpvProperty(const QString &name) const
{
    mpv_node node;
    if (mpv_get_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_NODE, &node) < 0)
        return {};
    QVariant value = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return value;
}

QString MpvWidget::mpvPropertyString(const QString &name) const
{
    char *value = mpv_get_property_string(m_mpv, name.toUtf8().constData());
    if (!value)
        return {};
    QString result = QString::fromUtf8(value);
    mpv_free(value);
    return result;
}

void MpvWidget::setMpvProperty(const QString &name, const QString &value)
{
    // mpv copies the data before returning, so temporaries are fine here.
    const QByteArray utf8 = value.toUtf8();
    const char *data = utf8.constData();
    mpv_set_property_async(m_mpv, 0, name.toUtf8().constData(), MPV_FORMAT_STRING, &data);
}

QList<QVariantMap> MpvWidget::tracks(const QString &type) const
{
    QList<QVariantMap> result;
    for (const QVariant &entry : mpvProperty(QStringLiteral("track-list")).toList()) {
        QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("type")).toString() == type)
            result.append(std::move(track));
    }
    return result;
}

QString MpvWidget::trackLabel(const QVariantMap &track)
{
    QString label = QStringLiteral("#%1").arg(track.value(QStringLiteral("id")).toLongLong());
    const QString title = track.value(QStringLiteral("title")).toString();
    const QString lang = track.value(QStringLiteral("lang")).toString();
    const QString codec = track.value(QStringLiteral("codec")).toString();
    if (!title.isEmpty())
        label += QStringLiteral(": ") + title;
    if (!lang.isEmpty())
        label += QStringLiteral(" [%1]").arg(lang);
    if (!codec.isEmpty())
        label += QStringLiteral(" (%1)").arg(codec);
    if (track.value(QStringLiteral("external")).toBool())
        label += QStringLiteral(" - external");
    return label;
}

bool MpvWidget::isSubtitleFile(const QString &path)
{
    return kSubtitleExtensions.contains(QFileInfo(path).suffix().toLower());
}

QString MpvWidget::subtitleFileFilter()
{
    QStringList patterns;
    for (const QString &ext : kSubtitleExtensions)
        patterns.append(QStringLiteral("*.") + ext);
    return tr("Subtitles (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' ')));
}

void MpvWidget::initializeGL()
{
    mpv_opengl_init_params glInit{&getProcAddress, nullptr};
    std::vector<mpv_render_param> params{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
    };

    // Hardware decoding interop needs the native display connection.
#if QT_CONFIG(xcb)
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
        params.push_back({MPV_RENDER_PARAM_X11_DISPLAY, x11->display()});
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0) && QT_CONFIG(wayland)
    if (auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        params.push_back({MPV_RENDER_PARAM_WL_DISPLAY, wayland->display()});
#endif
    params.push_back({MPV_RENDER_PARAM_INVALID, nullptr});

    if (mpv_render_context_create(&m_renderCtx, m_mpv, params.data()) < 0)
        throw std::runtime_error("failed to initialize mpv GL context");

    mpv_render_context_set_update_callback(m_renderCtx, &MpvWidget::onMpvRenderUpdate, this);

    for (const QStringList &cmd : std::exchange(m_pendingLoads, {}))
        command(cmd);
}

void MpvWidget::paintGL()
{
    if (!m_renderCtx)
        return;

    QOpenGLFunctions *gl = context()->functions();
    if (m_idle) {
        // Nothing is loaded: show the skin's background instead of the last frame.
        const QColor background = palette().color(QPalette::Window);
        gl->glClearColor(background.redF(), background.greenF(), background.blueF(), 1);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{
        static_cast<int>(defaultFramebufferObject()),
        static_cast<int>(width() * dpr),
        static_cast<int>(height() * dpr),
        0,
    };
    int flipY = 1;
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_renderCtx, params);

    // mpv may leave the framebuffer's alpha channel at 0 under the video. Qt
    // blends this widget's texture when composing the window (e.g. with
    // overlays on top), which would then drop the video and keep only
    // subtitles, so force the alpha channel to opaque.
    gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    gl->glClearColor(0, 0, 0, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void MpvWidget::processMpvEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            const QString name = QString::fromUtf8(prop->name);
            if (name == QLatin1String("media-title")) {
                QString title;
                if (prop->format == MPV_FORMAT_STRING && prop->data)
                    title = QString::fromUtf8(*static_cast<char **>(prop->data));
                Q_EMIT titleChanged(title);
            } else {
                const QVariant value = prop->format == MPV_FORMAT_NODE
                    ? nodeToVariant(static_cast<mpv_node *>(prop->data))
                    : QVariant();
                if (name == QLatin1String("idle-active")) {
                    m_idle = value.toBool();
                    update();
                } else if (name == QLatin1String("playlist-pos") && value.toInt() >= 0) {
                    m_lastPlaylistPos = value.toInt();
                }
                Q_EMIT propertyUpdated(name, value);
                if (!m_stateProperties.contains(name)) {
                    // mpv reports every observed property once on startup; that is not a change.
                    if (!m_initializedProperties.contains(name))
                        m_initializedProperties.insert(name);
                    else if (value.isValid())
                        Q_EMIT propertyChanged(name, value);
                }
            }
            break;
        }
        case MPV_EVENT_START_FILE:
            m_fileLoaded = false;
            m_seeking = false;
            m_awaitingVideoSize = true;
            Q_EMIT fileStarted();
            break;
        case MPV_EVENT_FILE_LOADED:
            m_fileLoaded = true;
            for (const QString &subtitle : std::exchange(m_pendingSubtitles, {}))
                addSubtitle(subtitle);
            break;
        case MPV_EVENT_SEEK:
            m_seeking = m_fileLoaded;
            break;
        case MPV_EVENT_VIDEO_RECONFIG:
            if (m_awaitingVideoSize) {
                const QSize size(mpvProperty(QStringLiteral("dwidth")).toInt(),
                                 mpvProperty(QStringLiteral("dheight")).toInt());
                if (!size.isEmpty()) {
                    m_awaitingVideoSize = false;
                    Q_EMIT videoSizeKnown(size);
                }
            }
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            if (m_seeking) {
                m_seeking = false;
                Q_EMIT seeked();
            }
            break;
        default:
            break;
        }
    }
}

void MpvWidget::onRenderUpdate()
{
    update();
}

void MpvWidget::onMpvWakeup(void *ctx)
{
    // Called from an mpv thread: hop back onto the GUI thread.
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "processMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::onMpvRenderUpdate(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onRenderUpdate", Qt::QueuedConnection);
}
