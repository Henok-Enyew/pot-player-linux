// Drives the real main window through its buttons and keyboard, and checks
// the result on mpv's properties. Needs a display (run under xvfb-run) and
// ffmpeg, which generates the test clip.

#include "Icons.h"
#include "MainWindow.h"
#include "MpvWidget.h"
#include "SeekBar.h"
#include "ThumbnailGenerator.h"
#include "TestClip.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <clocale>
#include <cmath>

namespace {

constexpr int kEntries = 3;

QImage iconImage(const QIcon &icon)
{
    return icon.pixmap(QSize(20, 20)).toImage();
}

} // namespace

class TransportTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void playPauseButton();
    void stopButton();
    void previousNextButtons();
    void muteButton();
    void mediaKeys();
    void seekHotkeys_data();
    void seekHotkeys();
    void volumeHotkeys();
    void pageUpPageDown();
    void playlistKeepsArrowKeys();
    void clickTogglesPause();
    void doubleClickTogglesFullScreen();
    void aboutDialog();
    void thumbnailsOnlyOnHover();

private:
    QVariant prop(const char *name) const { return m_mpv->mpvProperty(QString::fromLatin1(name)); }
    void set(const char *name, const QString &value) { m_mpv->setMpvProperty(QString::fromLatin1(name), value); }
    QWidget *keyTarget() const;
    void press(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    // Media keys have no text, which QTest::keyClick() refuses.
    void pressMediaKey(int key);
    void click(const char *buttonName);
    // Pauses playback at `seconds` of the current entry.
    void pauseAt(double seconds);

    QTemporaryDir m_dir;
    QString m_clip;
    MainWindow *m_window = nullptr;
    MpvWidget *m_mpv = nullptr;
};

void TransportTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_clip = m_dir.filePath(QStringLiteral("clip.mkv"));
    if (!makeTestClip(m_clip))
        QSKIP("ffmpeg is needed to generate the test clip");
}

void TransportTest::init()
{
    m_window = new MainWindow;
    m_window->resize(800, 450);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    m_mpv = m_window->findChild<MpvWidget *>();
    QVERIFY(m_mpv);

    m_window->openFiles(QStringList(kEntries, m_clip));
    QTRY_COMPARE_WITH_TIMEOUT(prop("playlist-count").toInt(), kEntries, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(prop("time-pos").isValid(), 10000);
    QTRY_VERIFY(!m_mpv->isIdle());
    QCOMPARE(prop("playlist-pos").toInt(), 0);
}

void TransportTest::cleanup()
{
    delete m_window;
    m_window = nullptr;
    m_mpv = nullptr;
}

QWidget *TransportTest::keyTarget() const
{
    QWidget *focus = QApplication::focusWidget();
    return focus ? focus : m_window;
}

void TransportTest::press(int key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(keyTarget(), static_cast<Qt::Key>(key), modifiers);
}

void TransportTest::pressMediaKey(int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(keyTarget(), &press);
    QApplication::sendEvent(keyTarget(), &release);
}

void TransportTest::click(const char *buttonName)
{
    auto *button = m_window->findChild<QToolButton *>(QString::fromLatin1(buttonName));
    QVERIFY2(button, buttonName);
    QTest::mouseClick(button, Qt::LeftButton);
}

void TransportTest::pauseAt(double seconds)
{
    set("pause", QStringLiteral("yes"));
    m_mpv->command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("absolute")});
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(prop("time-pos").toDouble() - seconds) < 0.5, 5000);
    QTRY_VERIFY(prop("pause").toBool());
}

void TransportTest::playPauseButton()
{
    auto *button = m_window->findChild<QToolButton *>(QStringLiteral("PlayButton"));
    QVERIFY(button);
    QTRY_COMPARE(iconImage(button->icon()), iconImage(skinIcon(IconType::Pause)));

    click("PlayButton");
    QTRY_VERIFY(prop("pause").toBool());
    QTRY_COMPARE(iconImage(button->icon()), iconImage(skinIcon(IconType::Play)));

    click("PlayButton");
    QTRY_VERIFY(!prop("pause").toBool());
    QTRY_COMPARE(iconImage(button->icon()), iconImage(skinIcon(IconType::Pause)));
}

void TransportTest::stopButton()
{
    pauseAt(100);
    if (QTest::currentTestFailed())
        return;
    auto *timeLabel = m_window->findChild<QLabel *>(QStringLiteral("TimeLabel"));
    QVERIFY(timeLabel);
    QTRY_VERIFY(timeLabel->text().contains(QStringLiteral("00:01:40")));

    click("StopButton");
    QTRY_VERIFY(m_mpv->isIdle());
    // The playlist survives, the seekbar and time rewind, and the video is blanked.
    QCOMPARE(prop("playlist-count").toInt(), kEntries);
    QTRY_COMPARE(timeLabel->text().count(QStringLiteral("00:00:00")), 2);
    auto *playButton = m_window->findChild<QToolButton *>(QStringLiteral("PlayButton"));
    QTRY_COMPARE(iconImage(playButton->icon()), iconImage(skinIcon(IconType::Play)));
    const QImage frame = m_mpv->grabFramebuffer();
    QCOMPARE(frame.pixelColor(frame.rect().center()).rgb(), m_mpv->palette().color(QPalette::Window).rgb());

    // Play starts the stopped entry again.
    click("PlayButton");
    QTRY_VERIFY(!m_mpv->isIdle());
    QTRY_COMPARE(prop("playlist-pos").toInt(), 0);
    QTRY_VERIFY(!prop("pause").toBool());
}

void TransportTest::previousNextButtons()
{
    click("NextButton");
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
    click("NextButton");
    QTRY_COMPARE(prop("playlist-pos").toInt(), 2);
    click("PreviousButton");
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);

    // mpv has no current entry once stopped; the buttons step from the last one.
    click("StopButton");
    QTRY_VERIFY(m_mpv->isIdle());
    click("NextButton");
    QTRY_VERIFY(!m_mpv->isIdle());
    QTRY_COMPARE(prop("playlist-pos").toInt(), 2);
    click("StopButton");
    QTRY_VERIFY(m_mpv->isIdle());
    click("PreviousButton");
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
}

void TransportTest::muteButton()
{
    auto *button = m_window->findChild<QToolButton *>(QStringLiteral("MuteButton"));
    QVERIFY(button);
    QVERIFY(!prop("mute").toBool());

    click("MuteButton");
    QTRY_VERIFY(prop("mute").toBool());
    QTRY_COMPARE(iconImage(button->icon()), iconImage(skinIcon(IconType::Muted)));

    press(Qt::Key_M);
    QTRY_VERIFY(!prop("mute").toBool());
    QTRY_COMPARE(iconImage(button->icon()), iconImage(skinIcon(IconType::Volume)));
}

void TransportTest::mediaKeys()
{
    pressMediaKey(Qt::Key_MediaTogglePlayPause);
    QTRY_VERIFY(prop("pause").toBool());
    pressMediaKey(Qt::Key_MediaTogglePlayPause);
    QTRY_VERIFY(!prop("pause").toBool());

    pressMediaKey(Qt::Key_MediaPause);
    QTRY_VERIFY(prop("pause").toBool());
    pressMediaKey(Qt::Key_MediaPause);
    QTest::qWait(100);
    QVERIFY(prop("pause").toBool());
    pressMediaKey(Qt::Key_MediaPlay);
    QTRY_VERIFY(!prop("pause").toBool());

    pressMediaKey(Qt::Key_MediaNext);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
    pressMediaKey(Qt::Key_MediaPrevious);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 0);
    pressMediaKey(Qt::Key_MediaNext);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);

    pressMediaKey(Qt::Key_MediaStop);
    QTRY_VERIFY(m_mpv->isIdle());
    QCOMPARE(prop("playlist-count").toInt(), kEntries);
    // Pause while stopped must not make the next start paused.
    pressMediaKey(Qt::Key_MediaPause);
    pressMediaKey(Qt::Key_MediaPlay);
    QTRY_VERIFY(!m_mpv->isIdle());
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
    QTRY_VERIFY(prop("time-pos").isValid());
    QVERIFY(!prop("pause").toBool());
}

void TransportTest::seekHotkeys_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<double>("delta");

    QTest::newRow("Right") << int(Qt::Key_Right) << int(Qt::NoModifier) << 5.0;
    QTest::newRow("Left") << int(Qt::Key_Left) << int(Qt::NoModifier) << -5.0;
    QTest::newRow("Ctrl+Right") << int(Qt::Key_Right) << int(Qt::ControlModifier) << 30.0;
    QTest::newRow("Ctrl+Left") << int(Qt::Key_Left) << int(Qt::ControlModifier) << -30.0;
    QTest::newRow("Shift+Right") << int(Qt::Key_Right) << int(Qt::ShiftModifier) << 60.0;
    QTest::newRow("Shift+Left") << int(Qt::Key_Left) << int(Qt::ShiftModifier) << -60.0;
}

void TransportTest::seekHotkeys()
{
    QFETCH(int, key);
    QFETCH(int, modifiers);
    QFETCH(double, delta);

    pauseAt(200);
    if (QTest::currentTestFailed())
        return;
    press(key, Qt::KeyboardModifiers(modifiers));
    QTRY_VERIFY2_WITH_TIMEOUT(std::abs(prop("time-pos").toDouble() - (200 + delta)) < 0.5,
                              qPrintable(QString::number(prop("time-pos").toDouble())), 5000);
}

void TransportTest::volumeHotkeys()
{
    set("volume", QStringLiteral("50"));
    QTRY_COMPARE(prop("volume").toDouble(), 50.0);
    press(Qt::Key_Up);
    QTRY_COMPARE(prop("volume").toDouble(), 52.0);
    press(Qt::Key_Down);
    press(Qt::Key_Down);
    QTRY_COMPARE(prop("volume").toDouble(), 48.0);
}

void TransportTest::pageUpPageDown()
{
    // PotPlayer: PgDn plays the next file, PgUp the previous one.
    press(Qt::Key_PageDown);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
    press(Qt::Key_PageDown);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 2);
    press(Qt::Key_PageUp);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
}

void TransportTest::playlistKeepsArrowKeys()
{
    set("volume", QStringLiteral("50"));
    QTRY_COMPARE(prop("volume").toDouble(), 50.0);

    m_window->setPlaylistVisible(true);
    auto *view = m_window->findChild<QListWidget *>(QStringLiteral("PlaylistView"));
    QVERIFY(view);
    QTRY_COMPARE(view->count(), kEntries);
    QTRY_VERIFY(view->isVisible());
    view->setFocus();
    QTRY_VERIFY(view->hasFocus());
    view->setCurrentRow(0);

    // Up/Down move through the list and leave the volume alone.
    press(Qt::Key_Down);
    QCOMPARE(view->currentRow(), 1);
    press(Qt::Key_Down);
    QCOMPARE(view->currentRow(), 2);
    press(Qt::Key_Up);
    QCOMPARE(view->currentRow(), 1);
    QTest::qWait(200);
    QCOMPARE(prop("volume").toDouble(), 50.0);

    // Return plays the selected entry instead of toggling fullscreen.
    press(Qt::Key_Return);
    QTRY_COMPARE(prop("playlist-pos").toInt(), 1);
    QVERIFY(!m_window->isFullScreen());

    // Player keys that a list has no use for still reach the player.
    press(Qt::Key_M);
    QTRY_VERIFY(prop("mute").toBool());
    press(Qt::Key_M);
    QTRY_VERIFY(!prop("mute").toBool());

    // Clicking the video hands the arrow keys back to the player.
    QTest::mouseClick(m_mpv, Qt::LeftButton, Qt::NoModifier, m_mpv->rect().center());
    QTRY_VERIFY(m_mpv->hasFocus());
    press(Qt::Key_Up);
    QTRY_COMPARE(prop("volume").toDouble(), 52.0);

    // So does closing the drawer while the list has focus.
    view->setFocus();
    QTRY_VERIFY(view->hasFocus());
    m_window->setPlaylistVisible(false);
    QTRY_VERIFY(!view->hasFocus());
    press(Qt::Key_Down);
    QTRY_COMPARE(prop("volume").toDouble(), 50.0);
}

void TransportTest::clickTogglesPause()
{
    QVERIFY(!prop("pause").toBool());
    QTest::mouseClick(m_mpv, Qt::LeftButton, Qt::NoModifier, m_mpv->rect().center());
    // Not at once: the click could still become a double click.
    QVERIFY(!prop("pause").toBool());
    QTRY_VERIFY(prop("pause").toBool());
    // QTest stamps events with its own clock: space the clicks so they stay single.
    QTest::mouseClick(m_mpv, Qt::LeftButton, Qt::NoModifier, m_mpv->rect().center(),
                      QApplication::doubleClickInterval() + 100);
    QTRY_VERIFY(!prop("pause").toBool());
}

void TransportTest::doubleClickTogglesFullScreen()
{
    const QRect normalGeometry = m_window->geometry();

    QTest::mouseDClick(m_mpv, Qt::LeftButton, Qt::NoModifier, m_mpv->rect().center());
    QTRY_VERIFY(m_window->isFullScreen());
    // The double click's first click must not pause.
    QTest::qWait(QApplication::doubleClickInterval() + 300);
    QVERIFY(!prop("pause").toBool());

    QTest::mouseDClick(m_mpv, Qt::LeftButton, Qt::NoModifier, m_mpv->rect().center());
    QTRY_VERIFY(!m_window->isFullScreen());
    QTRY_COMPARE(m_window->geometry(), normalGeometry);
    QTest::qWait(QApplication::doubleClickInterval() + 300);
    QVERIFY(!prop("pause").toBool());

    // Esc leaves fullscreen even while another widget has the keyboard.
    m_window->toggleFullScreen();
    QTRY_VERIFY(m_window->isFullScreen());
    auto *view = m_window->findChild<QListWidget *>(QStringLiteral("PlaylistView"));
    QVERIFY(view);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_VERIFY(!m_window->isFullScreen());
    QTRY_COMPARE(m_window->geometry(), normalGeometry);
}

void TransportTest::aboutDialog()
{
    press(Qt::Key_F1);
    QDialog *about = nullptr;
    QTRY_VERIFY((about = m_window->findChild<QDialog *>(QStringLiteral("AboutDialog"))) && about->isVisible());
    QCOMPARE(about->windowTitle(), QStringLiteral("About Top Player"));

    auto *title = about->findChild<QLabel *>(QStringLiteral("AboutTitle"));
    QVERIFY(title);
    QCOMPARE(title->accessibleName(), QStringLiteral("Top Player — Version " APP_VERSION));
    auto *links = about->findChild<QLabel *>(QStringLiteral("AboutLinks"));
    QVERIFY(links);
    QVERIFY(links->openExternalLinks());
    QVERIFY(links->text().contains(QStringLiteral("https://github.com/Henok-Enyew/pot-player-linux")));
    QVERIFY(links->text().contains(QStringLiteral("https://t.me/enoch90s")));

    auto *qt = about->findChild<QLabel *>(QStringLiteral("AboutQtVersion"));
    QVERIFY(qt);
    QCOMPARE(qt->text(), QString::fromLatin1(qVersion()));
    auto *mpv = about->findChild<QLabel *>(QStringLiteral("AboutMpvVersion"));
    QVERIFY(mpv);
    QVERIFY2(mpv->text().startsWith(QStringLiteral("mpv ")), qPrintable(mpv->text()));
    auto *hwdec = about->findChild<QLabel *>(QStringLiteral("AboutHwdec"));
    QVERIFY(hwdec);
    QVERIFY(!hwdec->text().isEmpty());

    // F1 again doesn't stack a second dialog.
    m_window->showAbout();
    QCOMPARE(m_window->findChildren<QDialog *>(QStringLiteral("AboutDialog")).size(), 1);

    if (const QString dir = qEnvironmentVariable("TOPPLAYER_SCREENSHOTS"); !dir.isEmpty())
        about->grab().save(dir + QStringLiteral("/about.png"));
    about->close();
    QTRY_VERIFY(!m_window->findChild<QDialog *>(QStringLiteral("AboutDialog")));
}

void TransportTest::thumbnailsOnlyOnHover()
{
    auto *thumbnails = m_window->findChild<ThumbnailGenerator *>();
    auto *seekBar = m_window->findChild<SeekBar *>();
    QVERIFY(thumbnails && seekBar);
    // The playing file is known, but nothing is decoded for previews yet.
    QTRY_VERIFY(thumbnails->isAvailable());
    QTest::qWait(300);
    QVERIFY(!thumbnails->isOpen());

    QSignalSpy ready(thumbnails, &ThumbnailGenerator::thumbnailReady);
    QTest::mouseMove(seekBar, QPoint(seekBar->width() / 2, seekBar->height() / 2));
    QTRY_VERIFY(thumbnails->isOpen());
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 10000);
    QVERIFY(!ready.first().at(1).value<QImage>().isNull());

    // Stopping closes the preview decoder too.
    m_mpv->stop();
    QTRY_VERIFY(!thumbnails->isAvailable());
    QVERIFY(!thumbnails->isOpen());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // libmpv requires the C numeric locale; QApplication may have changed it.
    std::setlocale(LC_NUMERIC, "C");
    TransportTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_transport.moc"
