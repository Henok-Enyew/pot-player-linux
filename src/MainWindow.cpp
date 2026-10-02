#include "MainWindow.h"
#include "MpvWidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>

namespace {

constexpr double kSeekStepSeconds = 5.0;
constexpr double kVolumeStep = 5.0;
constexpr int kResizeMargin = 6;

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mpv(new MpvWidget(this))
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAcceptDrops(true);
    setMinimumSize(320, 180);
    setCentralWidget(m_mpv);

    connect(m_mpv, &MpvWidget::titleChanged, this, [this](const QString &title) {
        setWindowTitle(title);
    });
}

void MainWindow::openFile(const QString &pathOrUrl)
{
    m_mpv->loadFile(pathOrUrl);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        m_mpv->togglePause();
        break;
    case Qt::Key_Left:
        m_mpv->seekRelative(-kSeekStepSeconds);
        break;
    case Qt::Key_Right:
        m_mpv->seekRelative(kSeekStepSeconds);
        break;
    case Qt::Key_Up:
        m_mpv->adjustVolume(kVolumeStep);
        break;
    case Qt::Key_Down:
        m_mpv->adjustVolume(-kVolumeStep);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        toggleFullScreen();
        break;
    case Qt::Key_Escape:
        if (isFullScreen())
            showNormal();
        break;
    case Qt::Key_Q:
        close();
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

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
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
