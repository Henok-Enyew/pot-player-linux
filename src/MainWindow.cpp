#include "MainWindow.h"
#include "MpvWidget.h"
#include "OsdWidget.h"
#include "PlayerMenu.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kVolumeStep = 5.0;
constexpr int kResizeMargin = 6;
// Largest share of the screen's available area an automatic resize may use.
constexpr qreal kMaxScreenFraction = 0.9;

QString formatDelay(double seconds)
{
    const long long ms = std::llround(seconds * 1000);
    return QStringLiteral("%1%2 ms").arg(ms > 0 ? QStringLiteral("+") : QString()).arg(ms);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mpv(new MpvWidget(this))
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAcceptDrops(true);
    setMinimumSize(320, 180);
    setCentralWidget(m_mpv);

    m_osd = new OsdWidget(m_mpv);
    m_menu = new PlayerMenu(m_mpv, this);

    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty())
        m_mpv->setMpvProperty(QStringLiteral("screenshot-directory"), pictures);

    connect(m_mpv, &MpvWidget::titleChanged, this, [this](const QString &title) {
        setWindowTitle(title);
    });
    connect(m_mpv, &MpvWidget::propertyChanged, this, &MainWindow::showPropertyOsd);
    connect(m_mpv, &MpvWidget::seeked, this, &MainWindow::showSeekOsd);
    // Like PotPlayer, open each file at 100% of its video resolution.
    connect(m_mpv, &MpvWidget::videoSizeKnown, this, [this](const QSize &size) {
        if (!isFullScreen() && !isMaximized())
            resizeToVideo(size, 1.0);
    });
    connect(m_menu, &PlayerMenu::osdRequested, m_osd,
            [this](const QString &label, const QString &value) { m_osd->showValue(label, value); });
}

void MainWindow::openFile(const QString &pathOrUrl)
{
    m_mpv->loadFile(pathOrUrl);
}

void MainWindow::openFileDialog()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Open File"));
    if (!file.isEmpty())
        openFile(file);
}

void MainWindow::loadSubtitle(const QString &path)
{
    m_mpv->addSubtitle(path);
    m_osd->showValue(tr("Subtitle Loaded"), QFileInfo(path).fileName());
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::setAlwaysOnTop(bool onTop)
{
    // Changing window flags hides the window, so restore its geometry and show it again.
    const QRect geometry = this->geometry();
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    setGeometry(geometry);
    show();
}

void MainWindow::scaleToVideo(qreal scale)
{
    const int videoWidth = m_mpv->mpvProperty(QStringLiteral("dwidth")).toInt();
    const int videoHeight = m_mpv->mpvProperty(QStringLiteral("dheight")).toInt();
    if (videoWidth <= 0 || videoHeight <= 0)
        return;

    if (isFullScreen() || isMaximized())
        showNormal();
    if (resizeToVideo(QSize(videoWidth, videoHeight), scale))
        m_osd->showValue(tr("Window Size"), QStringLiteral("%1%").arg(qRound(scale * 100)));
}

bool MainWindow::resizeToVideo(const QSize &videoSize, qreal scale)
{
    QScreen *screen = this->screen();
    if (!screen || videoSize.isEmpty())
        return false;

    // Video pixels map 1:1 to device pixels at 100%.
    const qreal dpr = devicePixelRatioF();
    QSizeF target(videoSize.width() * scale / dpr, videoSize.height() * scale / dpr);
    const QRect available = screen->availableGeometry();
    const QSizeF limit = QSizeF(available.size()) * kMaxScreenFraction;
    if (target.width() > limit.width() || target.height() > limit.height())
        target.scale(limit, Qt::KeepAspectRatio);
    const QSize size = target.toSize().expandedTo(minimumSize());

    QRect frame(QPoint(), size);
    frame.moveCenter(geometry().center());
    // Keep the whole window on the screen it is on.
    frame.moveLeft(std::clamp(frame.left(), available.left(), std::max(available.left(), available.right() - size.width() + 1)));
    frame.moveTop(std::clamp(frame.top(), available.top(), std::max(available.top(), available.bottom() - size.height() + 1)));
    setGeometry(frame);
    return true;
}

void MainWindow::showPropertyOsd(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("volume")) {
        const double volume = value.toDouble();
        m_osd->showValue(tr("Volume"), QStringLiteral("%1%").arg(qRound(volume)), std::min(volume, 100.0) / 100.0);
    } else if (name == QLatin1String("mute")) {
        m_osd->showValue(tr("Mute"), value.toBool() ? tr("On") : tr("Off"));
    } else if (name == QLatin1String("speed")) {
        m_osd->showValue(tr("Speed"), QStringLiteral("%1x").arg(value.toDouble(), 0, 'f', 2));
    } else if (name == QLatin1String("pause")) {
        m_osd->showValue(value.toBool() ? tr("Pause") : tr("Play"));
    } else if (name == QLatin1String("audio-delay")) {
        m_osd->showValue(tr("Audio Delay"), formatDelay(value.toDouble()));
    } else if (name == QLatin1String("sub-delay")) {
        m_osd->showValue(tr("Subtitle Delay"), formatDelay(value.toDouble()));
    } else if (name == QLatin1String("sub-pos")) {
        m_osd->showValue(tr("Subtitle Position"), QStringLiteral("%1%").arg(qRound(value.toDouble())));
    } else if (name == QLatin1String("sub-scale")) {
        m_osd->showValue(tr("Subtitle Size"), QStringLiteral("%1%").arg(qRound(value.toDouble() * 100)));
    }
}

void MainWindow::showSeekOsd()
{
    m_osd->showTime(m_mpv->mpvProperty(QStringLiteral("time-pos")).toDouble(),
                    m_mpv->mpvProperty(QStringLiteral("duration")).toDouble());
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Everything else is a QAction shortcut registered by PlayerMenu.
    switch (event->key()) {
    case Qt::Key_Enter:
        toggleFullScreen();
        break;
    case Qt::Key_Escape:
        if (isFullScreen())
            showNormal();
        break;
    default:
        QMainWindow::keyPressEvent(event);
        return;
    }
    event->accept();
}

void MainWindow::wheelEvent(QWheelEvent *event)
{
    // One standard wheel notch is 120 units; scale to support high-resolution wheels.
    const int delta = event->angleDelta().y();
    if (delta != 0)
        m_mpv->adjustVolume(kVolumeStep * delta / 120.0);
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || isFullScreen() || !windowHandle()) {
        QMainWindow::mousePressEvent(event);
        return;
    }

    // Without a frame, let the compositor move or resize the window for us.
    const Qt::Edges edges = isMaximized() ? Qt::Edges() : edgesAt(mapFromGlobal(event->globalPosition().toPoint()));
    if (edges)
        windowHandle()->startSystemResize(edges);
    else
        windowHandle()->startSystemMove();
    event->accept();
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        toggleFullScreen();
        event->accept();
        return;
    }
    QMainWindow::mouseDoubleClickEvent(event);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    m_menu->popup(event->globalPos());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    // Subtitle files are added to the playing video, or to a video dropped with them.
    QString media;
    QStringList subtitles;
    for (const QUrl &url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (url.isLocalFile() && MpvWidget::isSubtitleFile(path))
            subtitles.append(path);
        else if (media.isEmpty())
            media = path;
    }
    if (!media.isEmpty()) {
        m_mpv->loadFile(media, subtitles);
    } else {
        for (const QString &subtitle : std::as_const(subtitles))
            loadSubtitle(subtitle);
    }
    event->acceptProposedAction();
}

Qt::Edges MainWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (pos.x() <= kResizeMargin)
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - kResizeMargin)
        edges |= Qt::RightEdge;
    if (pos.y() <= kResizeMargin)
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - kResizeMargin)
        edges |= Qt::BottomEdge;
    return edges;
}
