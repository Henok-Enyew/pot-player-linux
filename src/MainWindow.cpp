#include "MainWindow.h"
#include "MpvWidget.h"
#include "OsdWidget.h"
#include "PlayerMenu.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kVolumeStep = 5.0;
constexpr int kResizeMargin = 6;

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
    const qreal dpr = devicePixelRatioF();
    resize(qRound(videoWidth * scale / dpr), qRound(videoHeight * scale / dpr));
    m_osd->showValue(tr("Window Size"), QStringLiteral("%1%").arg(qRound(scale * 100)));
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
    const QUrl &url = urls.first();
    openFile(url.isLocalFile() ? url.toLocalFile() : url.toString());
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
