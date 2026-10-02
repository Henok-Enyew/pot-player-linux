#pragma once

#include <QOpenGLWidget>
#include <QSet>
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

    void loadFile(const QString &pathOrUrl);
    void adjustVolume(double delta);

    // Runs an mpv command asynchronously, e.g. {"seek", "5", "relative"}.
    void command(const QStringList &args);

    // Reads a property synchronously; maps and arrays become QVariantMap/QVariantList.
    QVariant mpvProperty(const QString &name) const;
    QString mpvPropertyString(const QString &name) const;
    // Sets a property asynchronously from its string form, e.g. ("speed", "1.5").
    void setMpvProperty(const QString &name, const QString &value);

Q_SIGNALS:
    void titleChanged(const QString &title);
    // Emitted when an observed property changes after its initial value is known.
    void propertyChanged(const QString &name, const QVariant &value);
    // Emitted once playback resumes after a user seek.
    void seeked();

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
    // Files requested before the GL context existed; loading them earlier
    // would make mpv's video output fail to initialize.
    QStringList m_pendingFiles;
    QSet<QString> m_initializedProperties;
    bool m_fileLoaded = false;
    bool m_seeking = false;
};
