#include "MpvWidget.h"

#include <QByteArray>
#include <QMetaObject>
#include <QOpenGLContext>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <stdexcept>
#include <vector>

namespace {

void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return nullptr;
    return reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name)));
}

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
    mpv_set_option_string(m_mpv, "terminal", "yes");
    mpv_set_option_string(m_mpv, "msg-level", "all=warn");

    if (mpv_initialize(m_mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");

    mpv_observe_property(m_mpv, 0, "media-title", MPV_FORMAT_STRING);
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

void MpvWidget::loadFile(const QString &pathOrUrl)
{
    command({QStringLiteral("loadfile"), pathOrUrl});
}

void MpvWidget::togglePause()
{
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvWidget::seekRelative(double seconds)
{
    command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("relative")});
}

void MpvWidget::adjustVolume(double delta)
{
    command({QStringLiteral("add"), QStringLiteral("volume"), QString::number(delta)});
}

void MpvWidget::command(const QStringList &args)
{
    std::vector<QByteArray> storage;
    storage.reserve(args.size());
    std::vector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (const QString &arg : args) {
        storage.push_back(arg.toUtf8());
        argv.push_back(storage.back().constData());
    }
    argv.push_back(nullptr);
    mpv_command_async(m_mpv, 0, argv.data());
}

void MpvWidget::initializeGL()
{
    mpv_opengl_init_params glInit{&getProcAddress, nullptr};
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (mpv_render_context_create(&m_renderCtx, m_mpv, params) < 0)
        throw std::runtime_error("failed to initialize mpv GL context");

    mpv_render_context_set_update_callback(m_renderCtx, &MpvWidget::onMpvRenderUpdate, this);
}

void MpvWidget::paintGL()
{
    if (!m_renderCtx)
        return;

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
            if (qstrcmp(prop->name, "media-title") == 0) {
                QString title;
                if (prop->format == MPV_FORMAT_STRING && prop->data)
                    title = QString::fromUtf8(*static_cast<char **>(prop->data));
                Q_EMIT titleChanged(title);
            }
            break;
        }
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
