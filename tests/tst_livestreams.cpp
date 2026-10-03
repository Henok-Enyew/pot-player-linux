// The Live TV & Radio browser: parsing iptv-org playlists and Radio-Browser
// JSON, the stream cache, and the dialog driven through the real window.
// The lists are seeded into the cache with entries that point at local test
// clips, so no network access is needed. Needs a display (run under
// xvfb-run) and ffmpeg, which generates the test clips.

#include "AudioController.h"
#include "LiveStreamDialog.h"
#include "MainWindow.h"
#include "MpvWidget.h"
#include "StreamCatalog.h"
#include "TestClip.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QUrl>

#include <clocale>

namespace {

const QByteArray kSampleM3u = QByteArrayLiteral(
    "#EXTM3U x-tvg-url=\"https://example.com/guide.xml\"\n"
    "#EXTINF:-1 tvg-id=\"EBC.et\" tvg-logo=\"%LOGO%\" group-title=\"General\",EBC (720p)\n"
    "%CLIP%\n"
    "#EXTINF:-1 tvg-id=\"Fana.et\" tvg-language=\"Amharic\" group-title=\"News;Entertainment\",Fana TV, Addis (1080p) [Not 24/7]\n"
    "#EXTVLCOPT:http-user-agent=Mozilla/5.0\n"
    "https://example.com/fana/index.m3u8\n"
    "\n"
    "#EXTINF:-1,Walta\n"
    "https://example.com/walta.m3u8\n");

const QByteArray kSampleRadio = QByteArrayLiteral(
    "[{\"name\": \" Sheger FM 102.1 \", \"url\": \"http://example.com/sheger.pls\", \"url_resolved\": \"%TONE%\","
    " \"favicon\": \"\", \"tags\": \"news, talk,,music\", \"bitrate\": 128, \"country\": \"Ethiopia\", \"language\": \"amharic\"},"
    " {\"name\": \"Afro FM\", \"url\": \"https://example.com/afro.mp3\", \"url_resolved\": \"\","
    " \"favicon\": \"https://example.com/afro.png\", \"tags\": \"\", \"bitrate\": 0, \"country\": \"Ethiopia\"},"
    " {\"name\": \"No URL\", \"url\": \"\", \"url_resolved\": \"\"}]");

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool makeTone(const QString &path)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return false;
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=duration=30"), path});
    return process.waitForFinished(60000) && process.exitCode() == 0;
}

} // namespace

class LiveStreamTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void parseM3u();
    void parseRadioBrowser();
    void countries();
    void searchFilter();
    void cacheFreshness();
    void staleCacheWhenOffline();
    void openWithShortcut();
    void playChannel();
    void addToCurrentPlaylist();
    void radioShowsAudioView();

private:
    QVariant prop(const char *name) const { return m_mpv->mpvProperty(QString::fromLatin1(name)); }
    // Opens the dialog with Ctrl+L and waits for the list.
    LiveStreamDialog *openDialog();
    QTreeWidgetItem *rowNamed(LiveStreamDialog *dialog, const QString &name) const;

    QTemporaryDir m_dir;
    QString m_clip;
    QString m_tone;
    QString m_logo;
    MainWindow *m_window = nullptr;
    MpvWidget *m_mpv = nullptr;
};

void LiveStreamTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_clip = m_dir.filePath(QStringLiteral("ebc.mkv"));
    m_tone = m_dir.filePath(QStringLiteral("sheger.wav"));
    if (!makeTestClip(m_clip, 60) || !makeTone(m_tone))
        QSKIP("ffmpeg is needed to generate the test clips");
    m_logo = m_dir.filePath(QStringLiteral("logo.png"));
    QImage logo(64, 32, QImage::Format_ARGB32);
    logo.fill(Qt::red);
    QVERIFY(logo.save(m_logo));
}

void LiveStreamTest::init()
{
    // Fresh copies of the lists, as if downloaded a moment ago.
    QByteArray m3u = kSampleM3u;
    m3u.replace("%CLIP%", m_clip.toUtf8()).replace("%LOGO%", QUrl::fromLocalFile(m_logo).toEncoded());
    QVERIFY(writeFile(StreamCatalog::cacheFile(QStringLiteral("tv-et.m3u")), m3u));
    QVERIFY(writeFile(StreamCatalog::cacheFile(QStringLiteral("radio-et.json")),
                      QByteArray(kSampleRadio).replace("%TONE%", m_tone.toUtf8())));

    m_window = new MainWindow;
    m_window->resize(800, 500);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    m_mpv = m_window->findChild<MpvWidget *>();
    // Without a sound card, audio-only files would end at once.
    m_mpv->setMpvProperty(QStringLiteral("ao"), QStringLiteral("null"));
    m_mpv->setMpvProperty(QStringLiteral("pause"), QStringLiteral("yes"));
}

void LiveStreamTest::cleanup()
{
    delete m_window;
    m_window = nullptr;
    QDir(StreamCatalog::cacheDir()).removeRecursively();
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                  + QStringLiteral("/potplayer-linux/settings.ini"));
}

LiveStreamDialog *LiveStreamTest::openDialog()
{
    // Shortcuts need the window to be active.
    m_window->activateWindow();
    if (!QTest::qWaitForWindowActive(m_window))
        return nullptr;
    QTest::keyClick(m_mpv, Qt::Key_L, Qt::ControlModifier);
    LiveStreamDialog *dialog = m_window->liveStreamDialog();
    if (!dialog)
        return nullptr;
    const bool ready = QTest::qWaitFor([dialog] { return dialog->isVisible() && !dialog->isLoading(); }, 5000);
    return ready && QTest::qWaitForWindowExposed(dialog) ? dialog : nullptr;
}

QTreeWidgetItem *LiveStreamTest::rowNamed(LiveStreamDialog *dialog, const QString &name) const
{
    const QList<QTreeWidgetItem *> found = dialog->view()->findItems(name, Qt::MatchExactly);
    return found.isEmpty() ? nullptr : found.first();
}

void LiveStreamTest::parseM3u()
{
    const QList<StreamCatalog::Station> channels = StreamCatalog::parseM3u(kSampleM3u);
    QCOMPARE(channels.size(), 3);
    QCOMPARE(channels[0].name, QStringLiteral("EBC (720p)"));
    QCOMPARE(channels[0].url, QStringLiteral("%CLIP%"));
    QCOMPARE(channels[0].logo, QStringLiteral("%LOGO%"));
    QCOMPARE(channels[0].genre, QStringLiteral("General"));
    QCOMPARE(channels[0].quality, QStringLiteral("720p"));
    // Commas in the title, multiple groups, and option lines between #EXTINF and the URL.
    QCOMPARE(channels[1].name, QStringLiteral("Fana TV, Addis (1080p) [Not 24/7]"));
    QCOMPARE(channels[1].genre, QStringLiteral("News, Entertainment"));
    QCOMPARE(channels[1].language, QStringLiteral("Amharic"));
    QCOMPARE(channels[1].quality, QStringLiteral("1080p"));
    QCOMPARE(channels[1].url, QStringLiteral("https://example.com/fana/index.m3u8"));
    QCOMPARE(channels[2].name, QStringLiteral("Walta"));
    QVERIFY(channels[2].logo.isEmpty());
    // A plain list of URLs works too.
    const auto plain = StreamCatalog::parseM3u("http://a.example/1\r\nhttp://a.example/2\r\n");
    QCOMPARE(plain.size(), 2);
    QCOMPARE(plain[1].name, QStringLiteral("http://a.example/2"));
}

void LiveStreamTest::parseRadioBrowser()
{
    const QList<StreamCatalog::Station> stations = StreamCatalog::parseRadioBrowser(kSampleRadio);
    QCOMPARE(stations.size(), 2); // the entry without a URL is skipped
    QCOMPARE(stations[0].name, QStringLiteral("Sheger FM 102.1"));
    QCOMPARE(stations[0].url, QStringLiteral("%TONE%"));
    QCOMPARE(stations[0].genre, QStringLiteral("news, talk, music"));
    QCOMPARE(stations[0].bitrate, 128);
    QCOMPARE(stations[0].country, QStringLiteral("Ethiopia"));
    QCOMPARE(stations[1].url, QStringLiteral("https://example.com/afro.mp3")); // url_resolved empty
    QCOMPARE(stations[1].logo, QStringLiteral("https://example.com/afro.png"));
    QVERIFY(StreamCatalog::parseRadioBrowser("not json").isEmpty());
}

void LiveStreamTest::countries()
{
    const QList<StreamCatalog::Country> list = StreamCatalog::countries();
    QCOMPARE(list.first().code, QStringLiteral("et"));
    for (int i = 2; i < list.size(); ++i)
        QVERIFY2(QString::localeAwareCompare(list[i - 1].name, list[i].name) < 0, qPrintable(list[i].name));
    QCOMPARE(StreamCatalog::tvCountryUrl(QStringLiteral("ET")).toString(),
             QStringLiteral("https://iptv-org.github.io/iptv/countries/et.m3u"));
    QVERIFY(StreamCatalog::radioCountryUrl(QStringLiteral("et")).toString().contains(QLatin1String("radio-browser.info")));
}

void LiveStreamTest::searchFilter()
{
    const StreamCatalog::Station fana = StreamCatalog::parseM3u(kSampleM3u)[1];
    QVERIFY(StreamCatalog::matches(fana, QString()));
    QVERIFY(StreamCatalog::matches(fana, QStringLiteral("fana")));
    QVERIFY(StreamCatalog::matches(fana, QStringLiteral("amharic news"))); // language and genre
    QVERIFY(!StreamCatalog::matches(fana, QStringLiteral("sports")));
}

void LiveStreamTest::cacheFreshness()
{
    QVERIFY(StreamCatalog::cacheDir().startsWith(qEnvironmentVariable("XDG_CACHE_HOME")));
    QVERIFY(StreamCatalog::isCacheFresh(QStringLiteral("tv-et.m3u")));
    QVERIFY(!StreamCatalog::isCacheFresh(QStringLiteral("tv-us.m3u")));
    QFile file(StreamCatalog::cacheFile(QStringLiteral("tv-et.m3u")));
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-25 * 3600), QFileDevice::FileModificationTime));
    file.close();
    QVERIFY(!StreamCatalog::isCacheFresh(QStringLiteral("tv-et.m3u")));
}

void LiveStreamTest::staleCacheWhenOffline()
{
    // A refresh that can't reach the server falls back to the cached copy.
    StreamFetcher fetcher;
    QSignalSpy loaded(&fetcher, &StreamFetcher::loaded);
    QSignalSpy failed(&fetcher, &StreamFetcher::failed);
    fetcher.fetch(QStringLiteral("tv-et.m3u"), QUrl(QStringLiteral("http://127.0.0.1:1/et.m3u")), true);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size() + failed.size(), 1, 10000);
    QCOMPARE(loaded.size(), 1);
    QVERIFY(loaded.first().at(3).toBool()); // fromCache
    QVERIFY(loaded.first().at(1).toByteArray().contains("EBC"));

    // Without a cached copy, it fails.
    fetcher.fetch(QStringLiteral("tv-xx.m3u"), QUrl(QStringLiteral("http://127.0.0.1:1/xx.m3u")), true);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
}

void LiveStreamTest::openWithShortcut()
{
    LiveStreamDialog *dialog = openDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->source(), LiveStreamDialog::Tv);
    QCOMPARE(dialog->country(), QStringLiteral("et"));
    auto *country = dialog->findChild<QComboBox *>(QStringLiteral("LiveStreamCountry"));
    QCOMPARE(country->itemText(0), QStringLiteral("Ethiopia"));
    QCOMPARE(dialog->view()->topLevelItemCount(), 3);
    QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("LiveStreamStatus"))->text().startsWith(QLatin1String("3 channels")));

    QTreeWidgetItem *ebc = rowNamed(dialog, QStringLiteral("EBC (720p)"));
    QVERIFY(ebc);
    QCOMPARE(ebc->text(1), QStringLiteral("General"));
    QCOMPARE(ebc->text(2), QStringLiteral("720p"));
    // The logo replaces the placeholder once loaded (64x32 red, centered).
    QTRY_VERIFY(ebc->icon(0).pixmap(24, 24).toImage().pixelColor(12, 12) == QColor(Qt::red));
    QVERIFY(QDir(StreamCatalog::cacheDir() + QStringLiteral("/logos")).count() > 2);

    // The search box filters as you type.
    dialog->findChild<QLineEdit *>(QStringLiteral("LiveStreamFilter"))->setText(QStringLiteral("amharic"));
    QCOMPARE(dialog->visibleStations().size(), 1);
    QCOMPARE(dialog->visibleStations().first().name, QStringLiteral("Fana TV, Addis (1080p) [Not 24/7]"));
}

void LiveStreamTest::playChannel()
{
    LiveStreamDialog *dialog = openDialog();
    QVERIFY(dialog);
    QTreeWidgetItem *ebc = rowNamed(dialog, QStringLiteral("EBC (720p)"));
    dialog->view()->setCurrentItem(ebc);
    // A double-click is a press, a release and a second press seen as a double-click.
    const QPoint center = dialog->view()->visualItemRect(ebc).center();
    QTest::mouseClick(dialog->view()->viewport(), Qt::LeftButton, {}, center);
    QTest::mouseDClick(dialog->view()->viewport(), Qt::LeftButton, {}, center);
    QTRY_COMPARE_WITH_TIMEOUT(prop("path").toString(), m_clip, 10000);
    QTRY_COMPARE(prop("media-title").toString(), QStringLiteral("EBC (720p)"));

    // Other files get their own titles again.
    m_window->openFile(m_tone);
    QTRY_COMPARE_WITH_TIMEOUT(prop("path").toString(), m_tone, 10000);
    QTRY_COMPARE(prop("media-title").toString(), QStringLiteral("sheger.wav"));
}

void LiveStreamTest::addToCurrentPlaylist()
{
    m_window->openFile(m_tone);
    QTRY_COMPARE_WITH_TIMEOUT(prop("playlist-count").toInt(), 1, 10000);
    LiveStreamDialog *dialog = openDialog();
    QVERIFY(dialog);
    dialog->view()->setCurrentItem(rowNamed(dialog, QStringLiteral("Walta")));
    dialog->findChild<QPushButton *>(QStringLiteral("LiveStreamQueue"))->click();
    QTRY_COMPARE(prop("playlist-count").toInt(), 2);
    QCOMPARE(prop("playlist/1/filename").toString(), QStringLiteral("https://example.com/walta.m3u8"));
    QCOMPARE(prop("path").toString(), m_tone); // still playing
}

void LiveStreamTest::radioShowsAudioView()
{
    LiveStreamDialog *dialog = openDialog();
    QVERIFY(dialog);
    dialog->setSource(LiveStreamDialog::Radio);
    QTRY_VERIFY(!dialog->isLoading());
    QCOMPARE(dialog->view()->topLevelItemCount(), 2);
    QTreeWidgetItem *sheger = rowNamed(dialog, QStringLiteral("Sheger FM 102.1"));
    QVERIFY(sheger);
    QCOMPARE(sheger->text(1), QStringLiteral("news, talk, music · amharic"));
    QCOMPARE(sheger->text(2), QStringLiteral("128 kbps"));

    dialog->view()->setCurrentItem(sheger);
    dialog->findChild<QPushButton *>(QStringLiteral("LiveStreamPlay"))->click();
    QTRY_COMPARE_WITH_TIMEOUT(prop("path").toString(), m_tone, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(m_window->audio()->isActive(), 10000);
    QVERIFY(m_window->audio()->display() != AudioController::Display::None);

    // The tab is remembered.
    delete m_window;
    init();
    QCOMPARE(openDialog()->source(), LiveStreamDialog::Radio);
}

int main(int argc, char *argv[])
{
    // Keep the cache and settings away from the user's own.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/config")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", (home.path() + QStringLiteral("/cache")).toLocal8Bit());
    QApplication app(argc, argv);
    // libmpv requires the C numeric locale; QApplication may have changed it.
    std::setlocale(LC_NUMERIC, "C");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    LiveStreamTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_livestreams.moc"
