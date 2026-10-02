#pragma once

#include <QOpenGLWidget>
#include <QStringList>

struct mpv_handle;
struct mpv_render_context;

// An OpenGL surface that renders video through libmpv's render API.
class MpvWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    void loadFile(const QString &pathOrUrl);
    void togglePause();
    void seekRelative(double seconds);
    void adjustVolume(double delta);

    // Runs an mpv command asynchronously, e.g. {"seek", "5", "relative"}.
    void command(const QStringList &args);

Q_SIGNALS:
    void titleChanged(const QString &title);

protected:
    void initializeGL() override;
    void paintGL() override;

private Q_SLOTS:
    void processMpvEvents();
    void onRenderUpdate();

private:
    static void onMpvWakeup(void *ctx);
    static void onMpvRenderUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_renderCtx = nullptr;
};
