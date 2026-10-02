#include "MainWindow.h"
#include "ControlBar.h"
#include "MpvWidget.h"
#include "OsdWidget.h"
#include "PlayerMenu.h"
#include "PlaylistDrawer.h"
#include "SeekBar.h"
#include "ThumbnailGenerator.h"
#include "ThumbnailPopup.h"
#include "TitleBar.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr double kVolumeStep = 5.0;
constexpr int kResizeMargin = 6;
// Largest share of the screen's available area an automatic resize may use.
constexpr qreal kMaxScreenFraction = 0.9;
// Fullscreen controls and cursor hide after this long without mouse movement.
constexpr int kIdleHideMs = 2000;
// Distance between the seekbar and the thumbnail popup above it.
constexpr int kPopupGap = 6;

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
    setMinimumSize(420, 260);

    m_root = new QWidget(this);
    m_root->setObjectName(QStringLiteral("RootWidget"));
    m_titleBar = new TitleBar(this);
    m_controlBar = new ControlBar(m_mpv, m_root);
    m_drawer = new PlaylistDrawer(m_root);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(m_mpv, 1);
    body->addWidget(m_drawer);

    auto *layout = new QVBoxLayout(m_root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_titleBar);
    layout->addLayout(body, 1);
    layout->addWidget(m_controlBar);
    setCentralWidget(m_root);

    m_osd = new OsdWidget(m_mpv);
    m_menu = new PlayerMenu(m_mpv, this);
    m_titleBar->setTitle(QApplication::applicationDisplayName());

    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty())
        m_mpv->setMpvProperty(QStringLiteral("screenshot-directory"), pictures);

    connect(m_mpv, &MpvWidget::titleChanged, this, [this](const QString &title) {
        setWindowTitle(title);
        m_titleBar->setTitle(title.isEmpty() ? QApplication::applicationDisplayName() : title);
    });
    connect(m_mpv, &MpvWidget::propertyChanged, this, &MainWindow::showPropertyOsd);
    connect(m_mpv, &MpvWidget::propertyUpdated, this, &MainWindow::onStateUpdated);
    connect(m_mpv, &MpvWidget::seeked, this, &MainWindow::showSeekOsd);
    // Like PotPlayer, open each file at 100% of its video resolution.
    connect(m_mpv, &MpvWidget::videoSizeKnown, this, [this](const QSize &size) {
        if (!isFullScreen() && !isMaximized())
            resizeToVideo(size, 1.0);
    });
    connect(m_menu, &PlayerMenu::osdRequested, m_osd,
            [this](const QString &label, const QString &value) { m_osd->showValue(label, value); });
    connect(m_controlBar, &ControlBar::openRequested, this, &MainWindow::openFileDialog);
    connect(m_controlBar, &ControlBar::fullScreenRequested, this, &MainWindow::toggleFullScreen);
    connect(m_controlBar, &ControlBar::playlistToggled, this, &MainWindow::setPlaylistVisible);

    setupPlaylist();
    setupThumbnails();

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(kIdleHideMs);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (!isFullScreen())
            return;
        if (!m_controlBar->underMouse())
            m_controlBar->hide();
        m_mpv->setCursor(Qt::BlankCursor);
    });
    // Mouse moves over child widgets (the video, the bars) drive the fullscreen chrome.
    for (QWidget *widget : {static_cast<QWidget *>(m_mpv), m_root, static_cast<QWidget *>(m_controlBar)})
        widget->setMouseTracking(true);
    qApp->installEventFilter(this);
}

void MainWindow::setupPlaylist()
{
    connect(m_drawer, &PlaylistDrawer::playRequested, this, [this](int index) {
        m_mpv->command({QStringLiteral("playlist-play-index"), QString::number(index)});
    });
    connect(m_drawer, &PlaylistDrawer::moveRequested, this, [this](int from, int to) {
        m_mpv->command({QStringLiteral("playlist-move"), QString::number(from), QString::number(to)});
    });
    connect(m_drawer, &PlaylistDrawer::removeRequested, this, [this](QList<int> rows) {
        // Remove from the bottom up so earlier indexes stay valid.
        std::sort(rows.begin(), rows.end(), std::greater<>());
        for (int row : std::as_const(rows))
            m_mpv->command({QStringLiteral("playlist-remove"), QString::number(row)});
    });
    connect(m_drawer, &PlaylistDrawer::filesDropped, this,
            [this](const QStringList &files, int row) { m_mpv->insertFiles(files, row); });
    connect(m_drawer, &PlaylistDrawer::addRequested, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add to Playlist"));
        if (!files.isEmpty())
            m_mpv->insertFiles(files);
    });
    connect(m_drawer, &PlaylistDrawer::clearRequested, this,
            [this] { m_mpv->command({QStringLiteral("playlist-clear")}); });
    connect(m_drawer, &PlaylistDrawer::expandedChanged, m_controlBar, &ControlBar::setPlaylistChecked);
}

void MainWindow::setupThumbnails()
{
    m_thumbnails = new ThumbnailGenerator(this);
    m_thumbnailPopup = new ThumbnailPopup(m_root);

    SeekBar *seekBar = m_controlBar->seekBar();
    connect(seekBar, &SeekBar::hovered, this, [this, seekBar](double seconds, int x) {
        m_hoverSecond = static_cast<int>(seconds);
        m_thumbnailPopup->setTime(seconds);
        if (m_thumbnails->isAvailable())
            m_thumbnails->request(seconds);
        else
            m_thumbnailPopup->clearImage();
        m_popupAnchor = seekBar->mapTo(m_root, QPoint(x, -kPopupGap));
        m_thumbnailPopup->showAt(m_popupAnchor);
    });
    connect(seekBar, &SeekBar::hoverEnded, this, [this] {
        m_hoverSecond = -1;
        m_thumbnailPopup->hide();
    });
    connect(m_thumbnails, &ThumbnailGenerator::thumbnailReady, this, [this](int second, const QImage &image) {
        // Keep showing the previous frame until the one under the pointer arrives.
        if (m_hoverSecond < 0 || !m_thumbnailPopup->isVisible() || second != m_hoverSecond)
            return;
        m_thumbnailPopup->setImage(image);
        m_thumbnailPopup->showAt(m_popupAnchor);
    });
}

void MainWindow::onStateUpdated(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("playlist")) {
        m_drawer->setEntries(value.toList());
    } else if (name == QLatin1String("path")) {
        // Previews come from a second decoder, so only local files are worth it.
        const QString path = value.toString();
        m_thumbnails->setFile(QFileInfo(path).isFile() ? path : QString());
    }
}

void MainWindow::openFile(const QString &pathOrUrl)
{
    openFiles({pathOrUrl});
}

void MainWindow::openFiles(const QStringList &files)
{
    m_mpv->loadFiles(files);
}

void MainWindow::openFileDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open Files"));
    if (!files.isEmpty())
        openFiles(files);
}

bool MainWindow::isPlaylistVisible() const
{
    return m_drawer->isExpanded();
}

void MainWindow::setPlaylistVisible(bool visible)
{
    if (isFullScreen()) {
        m_playlistBeforeFullScreen = visible;
        m_controlBar->setPlaylistChecked(visible);
        return;
    }
    m_drawer->setExpanded(visible);
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
    // The title bar, control bar and playlist drawer surround the video.
    const QSize chrome = size() - m_mpv->size();
    const QSizeF limit = QSizeF(available.size()) * kMaxScreenFraction - QSizeF(chrome);
    if (target.width() > limit.width() || target.height() > limit.height())
        target.scale(limit, Qt::KeepAspectRatio);
    const QSize size = (target.toSize() + chrome).expandedTo(minimumSize());

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

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updateChrome();
}

void MainWindow::updateChrome()
{
    const bool fullScreen = isFullScreen();
    if (fullScreen == m_wasFullScreen)
        return;
    m_wasFullScreen = fullScreen;

    m_titleBar->setVisible(!fullScreen);
    m_controlBar->setVisible(!fullScreen);
    if (fullScreen) {
        m_playlistBeforeFullScreen = m_drawer->isExpanded();
        m_drawer->setExpanded(false, false);
        m_idleTimer.start();
    } else {
        m_idleTimer.stop();
        m_mpv->unsetCursor();
        m_drawer->setExpanded(m_playlistBeforeFullScreen, false);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseMove && watched->isWidgetType()
        && static_cast<QWidget *>(watched)->window() == this) {
        onMouseActivity(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onMouseActivity(const QPoint &globalPos)
{
    if (!isFullScreen())
        return;
    m_mpv->unsetCursor();
    // Reveal the controls when the pointer nears the bottom edge.
    const int revealHeight = m_controlBar->sizeHint().height() * 2;
    if (mapFromGlobal(globalPos).y() >= height() - revealHeight)
        m_controlBar->show();
    m_idleTimer.start();
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
    // Only the video area toggles fullscreen; the title bar maximizes instead.
    const QPoint videoPos = m_mpv->mapFromGlobal(event->globalPosition().toPoint());
    if (event->button() == Qt::LeftButton && m_mpv->rect().contains(videoPos)) {
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
