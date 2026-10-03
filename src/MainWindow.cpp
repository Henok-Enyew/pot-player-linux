#include "MainWindow.h"
#include "AboutDialog.h"
#include "AudioController.h"
#include "ControlBar.h"
#include "EmptyStateWidget.h"
#include "MediaFiles.h"
#include "MpvWidget.h"
#include "OsdWidget.h"
#include "PlayerMenu.h"
#include "PlaylistController.h"
#include "PlaylistDrawer.h"
#include "SeekBar.h"
#include "SubtitleDownloadDialog.h"
#include "ThumbnailGenerator.h"
#include "ThumbnailPopup.h"
#include "TitleBar.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
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
#include <utility>

namespace {

constexpr double kVolumeStep = 5.0;
constexpr int kResizeMargin = 6;
// Largest share of the screen's available area an automatic resize may use.
constexpr qreal kMaxScreenFraction = 0.9;
// Fullscreen controls and cursor hide after this long without mouse movement.
constexpr int kIdleHideMs = 2000;
// Distance between the seekbar and the thumbnail popup above it.
constexpr int kPopupGap = 6;

// Keys that a focused list keeps for its own navigation instead of letting the
// player's shortcuts (volume, fullscreen) take them.
bool isListNavigationKey(const QKeyEvent *event)
{
    if (event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier))
        return false;
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    default:
        return false;
    }
}

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
    // Clicking the video takes keyboard focus back from the playlist, so the
    // arrow keys control the player again.
    m_mpv->setFocusPolicy(Qt::ClickFocus);

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

    // Stacked over the video in creation order: the audio view, then the start
    // screen, then the OSD on top.
    m_audio = new AudioController(m_mpv, this);
    m_emptyState = new EmptyStateWidget(m_mpv);
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
    connect(m_audio, &AudioController::message, m_osd,
            [this](const QString &label, const QString &value) { m_osd->showValue(label, value); });
    connect(m_menu, &PlayerMenu::osdRequested, m_osd,
            [this](const QString &label, const QString &value) { m_osd->showValue(label, value); });
    connect(m_controlBar, &ControlBar::openRequested, this, &MainWindow::openFileDialog);
    connect(m_emptyState, &EmptyStateWidget::openFileRequested, this, &MainWindow::openFileDialog);
    connect(m_emptyState, &EmptyStateWidget::openFolderRequested, this, &MainWindow::openFolderDialog);
    connect(m_emptyState, &EmptyStateWidget::openUrlRequested, this, &MainWindow::openUrlDialog);
    connect(m_emptyState, &EmptyStateWidget::openPlaylistRequested, this, &MainWindow::openPlaylistDialog);
    connect(m_emptyState, &EmptyStateWidget::urlsDropped, this, &MainWindow::openUrls);
    connect(m_mpv, &MpvWidget::fileStarted, m_emptyState, [this] { m_emptyState->setActive(false); });
    connect(m_controlBar, &ControlBar::fullScreenRequested, this, &MainWindow::toggleFullScreen);
    connect(m_controlBar, &ControlBar::playlistToggled, this, &MainWindow::setPlaylistVisible);

    setupPlaylist();
    setupThumbnails();

    m_clickTimer.setSingleShot(true);
    connect(&m_clickTimer, &QTimer::timeout, this, [this] {
        if (!m_mpv->isIdle())
            m_mpv->togglePause();
    });

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
    m_playlist = new PlaylistController(m_mpv, m_drawer, this);
    connect(m_playlist, &PlaylistController::message, m_osd,
            [this](const QString &label, const QString &value) { m_osd->showValue(label, value); });
    connect(m_drawer, &PlaylistDrawer::openPlaylistRequested, this, &MainWindow::openPlaylistDialog);
    connect(m_drawer, &PlaylistDrawer::expandedChanged, m_controlBar, &ControlBar::setPlaylistChecked);
    connect(m_drawer, &PlaylistDrawer::expandedChanged, this, [this](bool expanded) {
        // Don't leave the keyboard on a list that is going away.
        if (!expanded && m_drawer->isAncestorOf(QApplication::focusWidget()))
            m_mpv->setFocus();
    });
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
    // An audio file's "video" is its cover or a visualization: no previews.
    connect(m_mpv, &MpvWidget::fileLoaded, this, [this] {
        if (m_mpv->isAudioOnly())
            m_thumbnails->setFile({});
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
    if (name == QLatin1String("idle-active")) {
        // Nothing loaded (startup, stop, or the playlist ran out or was cleared).
        // The report can arrive after the next file already started, so check again.
        if (value.toBool() && m_mpv->isIdle())
            m_emptyState->setActive(true);
    } else if (name == QLatin1String("playlist")) {
        m_playlist->setPlaylist(value.toList());
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
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open Files"), {}, MediaFiles::mediaFileFilter());
    if (!files.isEmpty())
        openFiles(files);
}

void MainWindow::openFolderDialog()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
    if (folder.isEmpty())
        return;
    m_osd->showValue(tr("Scanning Folder"), QFileInfo(folder).fileName());
    MediaFiles::expandFoldersAsync({folder}, this, [this, folder](const QStringList &files) {
        if (files.isEmpty())
            m_osd->showValue(tr("No media files in"), QFileInfo(folder).fileName());
        else
            openFiles(files);
    });
}

void MainWindow::openUrlDialog()
{
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Open URL / Stream"));
    dialog.setLabelText(tr("Video or audio URL (http, https, rtsp, rtmp, ...):"));
    dialog.setOkButtonText(tr("Open"));
    dialog.resize(520, dialog.sizeHint().height());
    // Offer a URL that is already on the clipboard.
    const QString clipboard = QGuiApplication::clipboard()->text().trimmed();
    if (clipboard.contains(QLatin1String("://")) && !clipboard.contains(QLatin1Char('\n')))
        dialog.setTextValue(clipboard);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString text = dialog.textValue().trimmed();
    if (text.isEmpty())
        return;
    // Accept "example.com/video.mp4" as well as full URLs, which mpv takes as typed.
    const bool hasScheme = text.contains(QLatin1String("://"));
    const QUrl url = hasScheme ? QUrl(text) : QUrl::fromUserInput(text);
    if (!url.isValid() || url.scheme().isEmpty()) {
        m_osd->showValue(tr("Invalid URL"), text);
        return;
    }
    if (url.isLocalFile())
        openFile(url.toLocalFile());
    else
        openFile(hasScheme ? text : url.toString());
}

void MainWindow::openPlaylistDialog()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Open Playlist"), {}, MediaFiles::playlistFileFilter());
    if (!file.isEmpty())
        m_mpv->loadPlaylist(file);
}

void MainWindow::savePlaylistDialog()
{
    m_playlist->savePlaylistDialog();
}

bool MainWindow::startSession(bool restore)
{
    return m_playlist->startSession(restore);
}

void MainWindow::openUrls(const QList<QUrl> &urls)
{
    // Subtitle files are added to the playing video, or to a video dropped with them.
    QStringList media;
    QStringList subtitles;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            media.append(url.toString());
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isDir())
            media.append(path); // expanded below, off the GUI thread
        else if (MpvWidget::isSubtitleFile(path))
            subtitles.append(path);
        else
            media.append(path);
    }
    if (!media.isEmpty()) {
        MediaFiles::expandFoldersAsync(media, this, [this, subtitles](const QStringList &files) {
            if (files.isEmpty())
                m_osd->showValue(tr("No media files found"));
            else
                m_mpv->loadFiles(files, subtitles);
        });
    } else if (!subtitles.isEmpty()) {
        for (const QString &subtitle : std::as_const(subtitles))
            loadSubtitle(subtitle);
    } else {
        m_osd->showValue(tr("No media files found"));
    }
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

void MainWindow::openSubtitleDownloadDialog()
{
    const QString path = m_mpv->isIdle() ? QString() : m_mpv->mpvPropertyString(QStringLiteral("path"));
    if (path.isEmpty()) {
        m_osd->showValue(tr("Open a video to download subtitles for"));
        return;
    }
    auto *dialog = new SubtitleDownloadDialog(path, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SubtitleDownloadDialog::subtitleDownloaded, this, [this, path](const QString &file) {
        // The video may have changed while the dialog was open.
        if (m_mpv->mpvPropertyString(QStringLiteral("path")) != path)
            return;
        m_mpv->addSubtitle(file);
        // A subtitle the user just asked for should show even if subtitles were hidden.
        m_mpv->setMpvProperty(QStringLiteral("sub-visibility"), QStringLiteral("yes"));
        m_osd->showValue(tr("Subtitle loaded:"), QFileInfo(file).fileName());
    });
    dialog->open();
}

void MainWindow::showAbout()
{
    if (m_about) {
        m_about->raise();
        m_about->activateWindow();
        return;
    }
    m_about = new AboutDialog(m_mpv, this);
    m_about->setAttribute(Qt::WA_DeleteOnClose);
    m_about->open();
}

void MainWindow::openSubtitleSettingsDialog()
{
    SubtitleSettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        exitFullScreen();
        return;
    }
    m_maximizedBeforeFullScreen = isMaximized();
    m_geometryBeforeFullScreen = m_maximizedBeforeFullScreen ? normalGeometry() : geometry();
    showFullScreen();
}

void MainWindow::exitFullScreen()
{
    if (!isFullScreen())
        return;
    if (m_maximizedBeforeFullScreen) {
        showMaximized();
        return;
    }
    showNormal();
    if (m_geometryBeforeFullScreen.isValid())
        setGeometry(m_geometryBeforeFullScreen);
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_playlist->saveSession();
    QMainWindow::closeEvent(event);
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
    // A shortcut fires unless the focused widget claims the key first. Let a
    // focused list (the playlist) keep its navigation keys, so Up/Down move the
    // selection instead of changing the volume.
    if (event->type() == QEvent::ShortcutOverride && qobject_cast<QAbstractItemView *>(watched)
        && static_cast<QWidget *>(watched)->window() == this
        && isListNavigationKey(static_cast<QKeyEvent *>(event))) {
        event->accept();
        return true;
    }
    // Esc always leaves fullscreen, whichever widget has the keyboard.
    if (event->type() == QEvent::KeyPress && isFullScreen() && watched->isWidgetType()
        && static_cast<QWidget *>(watched)->window() == this
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        exitFullScreen();
        return true;
    }
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
    case Qt::Key_MediaPlay:
        m_mpv->play();
        break;
    case Qt::Key_MediaPause:
        m_mpv->pause();
        break;
    case Qt::Key_MediaTogglePlayPause:
        m_mpv->togglePause();
        break;
    case Qt::Key_MediaStop:
        m_mpv->stop();
        break;
    case Qt::Key_MediaNext:
        m_mpv->playlistNext();
        break;
    case Qt::Key_MediaPrevious:
        m_mpv->playlistPrev();
        break;
    case Qt::Key_Enter:
        toggleFullScreen();
        break;
    case Qt::Key_Escape:
        exitFullScreen();
        break;
    default:
        QMainWindow::keyPressEvent(event);
        return;
    }
    event->accept();
}

void MainWindow::wheelEvent(QWheelEvent *event)
{
    // The playlist scrolls itself; at its ends the leftover wheel events land here.
    if (m_drawer->isVisible() && m_drawer->rect().contains(m_drawer->mapFromGlobal(event->globalPosition().toPoint()))) {
        event->ignore();
        return;
    }
    // One standard wheel notch is 120 units; scale to support high-resolution wheels.
    const int delta = event->angleDelta().y();
    if (delta != 0)
        m_mpv->adjustVolume(kVolumeStep * delta / 120.0);
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QMainWindow::mousePressEvent(event);
        return;
    }
    const QPoint globalPos = event->globalPosition().toPoint();
    const bool canMove = !isFullScreen() && windowHandle();

    // Without a frame, let the compositor move or resize the window for us.
    const Qt::Edges edges = !canMove || isMaximized() ? Qt::Edges() : edgesAt(mapFromGlobal(globalPos));
    if (edges) {
        windowHandle()->startSystemResize(edges);
    } else if (isOverVideo(globalPos)) {
        // Wait for the release (a click: pause) or for the pointer to move (a drag: move the window).
        m_videoPress = globalPos;
    } else if (canMove) {
        windowHandle()->startSystemMove();
    } else {
        QMainWindow::mousePressEvent(event);
        return;
    }
    event->accept();
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_videoPress && (event->buttons() & Qt::LeftButton)
        && (event->globalPosition().toPoint() - *m_videoPress).manhattanLength() >= QApplication::startDragDistance()) {
        m_videoPress.reset();
        if (!isFullScreen() && windowHandle())
            windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && std::exchange(m_videoPress, std::nullopt)) {
        // Toggling now would make every double click pause and resume playback.
        m_clickTimer.start(QApplication::doubleClickInterval());
        event->accept();
        return;
    }
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Only the video area toggles fullscreen; the title bar maximizes instead.
    if (event->button() == Qt::LeftButton && isOverVideo(event->globalPosition().toPoint())) {
        m_clickTimer.stop();
        m_videoPress.reset();
        toggleFullScreen();
        event->accept();
        return;
    }
    QMainWindow::mouseDoubleClickEvent(event);
}

bool MainWindow::isOverVideo(const QPoint &globalPos) const
{
    return m_mpv->isVisible() && m_mpv->rect().contains(m_mpv->mapFromGlobal(globalPos));
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
    openUrls(urls);
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
