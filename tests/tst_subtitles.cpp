// Subtitles: showing and hiding them, and the OpenSubtitles search and
// download dialog, run against a local mock of the OpenSubtitles REST API.
// Needs a display (run under xvfb-run) and ffmpeg, which generates the test clip.

#include "MainWindow.h"
#include "MpvWidget.h"
#include "OpenSubtitlesClient.h"
#include "OsdWidget.h"
#include "PlayerMenu.h"
#include "SubtitleDownloadDialog.h"
#include "SubtitleSearch.h"
#include "TestClip.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QUrlQuery>

#include <clocale>
#include <functional>

namespace {

const QByteArray kSrt = "1\n00:00:00,000 --> 00:09:00,000\nHello from the mock server\n";

// A minimal HTTP/1.1 server: one request per connection, answered by `handler`.
class MockServer : public QObject
{
public:
    struct Request {
        QByteArray method;
        QUrl url;
        QHash<QByteArray, QByteArray> headers; // lower-case names
        QByteArray body;
    };
    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/json";
        bool hold = false; // never answer (for cancel tests)
    };

    std::function<Response(const Request &)> handler;
    QList<Request> requests;

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }

    MockServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

private:
    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        Request request;
        const QList<QByteArray> lines = buffer.left(end).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        request.method = first.value(0);
        request.url = QUrl(QString::fromLatin1(first.value(1)));
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            if (colon > 0)
                request.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const qsizetype length = request.headers.value("content-length").toLongLong();
        if (buffer.size() < end + 4 + length)
            return;
        request.body = buffer.mid(end + 4, length);
        m_buffers.remove(socket);
        requests.append(request);

        const Response response = handler ? handler(request) : Response{404, "{}"};
        if (response.hold)
            return;
        QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
        reply += "Content-Type: " + response.contentType + "\r\n";
        reply += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n\r\n";
        reply += response.body;
        socket->write(reply);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

QJsonObject result(int fileId, const QString &language, const QString &fileName, int downloads, double rating,
                   bool hearingImpaired, bool hashMatch)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::number(fileId)},
        {QStringLiteral("type"), QStringLiteral("subtitle")},
        {QStringLiteral("attributes"), QJsonObject{
            {QStringLiteral("language"), language},
            {QStringLiteral("download_count"), downloads},
            {QStringLiteral("ratings"), rating},
            {QStringLiteral("hearing_impaired"), hearingImpaired},
            {QStringLiteral("moviehash_match"), hashMatch},
            {QStringLiteral("release"), fileName + QStringLiteral(" release")},
            {QStringLiteral("uploader"), QJsonObject{{QStringLiteral("name"), QStringLiteral("tester")}}},
            {QStringLiteral("files"), QJsonArray{QJsonObject{
                {QStringLiteral("file_id"), fileId},
                {QStringLiteral("file_name"), fileName},
            }}},
        }},
    };
}

} // namespace

class SubtitleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void parseFileName_data();
    void parseFileName();
    void movieHash();
    void showAndHideSubtitles();
    void dialogSearchesForPlayingFile();
    void downloadAndApply();
    void downloadToCache();
    void manualSearch();
    void queryEncoding();
    void busyAndCancel();
    void apiErrors();
    void missingApiKey();

private:
    QVariant prop(const char *name) const { return m_mpv->mpvProperty(QString::fromLatin1(name)); }
    QString propString(const char *name) const { return m_mpv->mpvPropertyString(QString::fromLatin1(name)); }
    QWidget *keyTarget() const;
    void press(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    QMenu *subtitleMenu() const;
    QAction *menuAction(QMenu *menu, const QString &text) const;
    // Opens the dialog with D and waits for its first search to finish.
    SubtitleDownloadDialog *openDialog();
    MockServer::Response defaultResponse(const MockServer::Request &request);

    QTemporaryDir m_dir;
    QString m_video;
    QString m_srt1;
    QString m_srt2;
    MockServer m_server;
    MainWindow *m_window = nullptr;
    MpvWidget *m_mpv = nullptr;
};

void SubtitleTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY(QDir().mkpath(m_dir.filePath(QStringLiteral("movies"))));
    m_video = m_dir.filePath(QStringLiteral("movies/The.Matrix.1999.1080p.BluRay.x264-GRP.mkv"));
    if (!makeTestClip(m_video))
        QSKIP("ffmpeg is needed to generate the test clip");
    m_srt1 = m_dir.filePath(QStringLiteral("first.srt"));
    m_srt2 = m_dir.filePath(QStringLiteral("second.srt"));
    for (const QString &path : {m_srt1, m_srt2}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kSrt);
    }

    QVERIFY(m_server.listen());
    qputenv("POTPLAYER_OPENSUBTITLES_URL", m_server.url(QStringLiteral("/api/v1")).toUtf8());
}

MockServer::Response SubtitleTest::defaultResponse(const MockServer::Request &request)
{
    const QString path = request.url.path();
    if (request.method == "GET" && path == QLatin1String("/api/v1/subtitles")) {
        const QJsonObject body{
            {QStringLiteral("total_count"), 3},
            {QStringLiteral("data"), QJsonArray{
                result(101, QStringLiteral("en"), QStringLiteral("The.Matrix.1999.Popular.srt"), 52000, 8.4, false, false),
                result(102, QStringLiteral("en"), QStringLiteral("The.Matrix.1999.Styled.ass"), 900, 0, true, false),
                result(103, QStringLiteral("en"), QStringLiteral("The.Matrix.1999.1080p.BluRay.x264-GRP.srt"), 300, 9.1, false, true),
            }},
        };
        return {200, QJsonDocument(body).toJson()};
    }
    if (request.method == "POST" && path == QLatin1String("/api/v1/download")) {
        const int fileId = QJsonDocument::fromJson(request.body).object().value(QStringLiteral("file_id")).toInt();
        const QJsonObject body{
            {QStringLiteral("link"), m_server.url(QStringLiteral("/files/%1").arg(fileId))},
            {QStringLiteral("file_name"), QStringLiteral("file.srt")},
            {QStringLiteral("remaining"), 19},
        };
        return {200, QJsonDocument(body).toJson()};
    }
    if (request.method == "GET" && path.startsWith(QLatin1String("/files/")))
        return {200, kSrt, "application/x-subrip"};
    return {404, "{\"message\":\"not found\"}"};
}

void SubtitleTest::init()
{
    SubtitleSearch::setUserApiKey(QStringLiteral("test-key"));
    SubtitleSearch::setLanguage(QStringLiteral("en"));
    SubtitleSearch::setSaveBesideVideo(true);
    m_server.requests.clear();
    m_server.handler = [this](const MockServer::Request &request) { return defaultResponse(request); };

    m_window = new MainWindow;
    m_window->resize(800, 450);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    m_mpv = m_window->findChild<MpvWidget *>();
    QVERIFY(m_mpv);
    m_mpv->setMpvProperty(QStringLiteral("pause"), QStringLiteral("yes"));
    m_window->openFile(m_video);
    QTRY_VERIFY_WITH_TIMEOUT(prop("time-pos").isValid(), 10000);
    m_mpv->setFocus();
}

void SubtitleTest::cleanup()
{
    // Close a dialog left open by a failed check.
    if (QWidget *modal = QApplication::activeModalWidget())
        modal->close();
    delete m_window;
    m_window = nullptr;
    m_mpv = nullptr;
    // Downloaded subtitles would otherwise be picked up next time.
    const QDir movies(m_dir.filePath(QStringLiteral("movies")));
    for (const QString &name : movies.entryList({QStringLiteral("*.srt"), QStringLiteral("*.ass")}, QDir::Files))
        QFile::remove(movies.filePath(name));
}

QWidget *SubtitleTest::keyTarget() const
{
    QWidget *focus = QApplication::focusWidget();
    return focus ? focus : m_window;
}

void SubtitleTest::press(int key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(keyTarget(), static_cast<Qt::Key>(key), modifiers);
}

QMenu *SubtitleTest::subtitleMenu() const
{
    for (QAction *action : m_window->findChild<PlayerMenu *>()->actions()) {
        if (action->menu() && action->text() == QLatin1String("Subtitles"))
            return action->menu();
    }
    return nullptr;
}

QAction *SubtitleTest::menuAction(QMenu *menu, const QString &text) const
{
    Q_EMIT menu->aboutToShow(); // track menus are filled as they open
    for (QAction *action : menu->actions()) {
        if (action->text() == text || (action->menu() && action->menu()->title() == text))
            return action;
    }
    return nullptr;
}

SubtitleDownloadDialog *SubtitleTest::openDialog()
{
    // Without a window manager, closing a dialog doesn't hand activation back.
    m_window->activateWindow();
    m_mpv->setFocus();
    [&] { QTRY_VERIFY(m_window->isActiveWindow()); }();
    press(Qt::Key_D);
    SubtitleDownloadDialog *dialog = nullptr;
    [&] { QTRY_VERIFY((dialog = m_window->findChild<SubtitleDownloadDialog *>()) && dialog->isVisible()); }();
    if (dialog)
        [&] { QTRY_VERIFY_WITH_TIMEOUT(!dialog->isBusy() && !dialog->results().isEmpty(), 10000); }();
    return dialog;
}

void SubtitleTest::parseFileName_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("title");
    QTest::addColumn<int>("year");
    QTest::addColumn<int>("season");
    QTest::addColumn<int>("episode");

    QTest::newRow("movie") << "The.Matrix.1999.1080p.BluRay.x264-GRP.mkv" << "The Matrix" << 1999 << -1 << -1;
    QTest::newRow("brackets") << "[YTS.MX] Dune Part Two (2024) [2160p].mp4" << "Dune Part Two" << 2024 << -1 << -1;
    QTest::newRow("episode") << "Breaking.Bad.S01E02.720p.HDTV.x264.mkv" << "Breaking Bad" << 0 << 1 << 2;
    QTest::newRow("1x02") << "the_office_2x05_hdtv.avi" << "the office" << 0 << 2 << 5;
    QTest::newRow("year title") << "2001.A.Space.Odyssey.1968.REMASTERED.mkv" << "2001 A Space Odyssey" << 1968 << -1 << -1;
    QTest::newRow("no tags") << "/home/me/Videos/My Holiday Video.mp4" << "My Holiday Video" << 0 << -1 << -1;
    QTest::newRow("tag only") << "Inception.WEB-DL.mkv" << "Inception" << 0 << -1 << -1;
}

void SubtitleTest::parseFileName()
{
    QFETCH(QString, file);
    QFETCH(QString, title);
    QFETCH(int, year);
    QFETCH(int, season);
    QFETCH(int, episode);
    const SubtitleSearch::ParsedName parsed = SubtitleSearch::parseFileName(file);
    QCOMPARE(parsed.title, title);
    QCOMPARE(parsed.year, year);
    QCOMPARE(parsed.season, season);
    QCOMPARE(parsed.episode, episode);
}

void SubtitleTest::movieHash()
{
    // Size plus the 64-bit words of the first and last 64 KiB: here the first
    // chunk is 0x01 bytes and the last is zeros.
    const QString path = m_dir.filePath(QStringLiteral("hash.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(65536, '\x01'));
    file.write(QByteArray(2 * 65536, '\0'));
    file.close();
    QCOMPARE(SubtitleSearch::movieHash(path), QStringLiteral("2020202020232000"));

    // Too small to hash.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArray(1000, 'x'));
    file.close();
    QCOMPARE(SubtitleSearch::movieHash(path), QString());
    QCOMPARE(SubtitleSearch::movieHash(m_dir.filePath(QStringLiteral("missing.mkv"))), QString());
}

void SubtitleTest::showAndHideSubtitles()
{
    m_window->loadSubtitle(m_srt1);
    QTRY_COMPARE(propString("sid"), QStringLiteral("1"));
    QVERIFY(prop("sub-visibility").toBool());
    QTRY_COMPARE(propString("sub-text"), QStringLiteral("Hello from the mock server"));

    // Alt+H hides and shows them, and the menu item follows.
    QMenu *subs = subtitleMenu();
    QVERIFY(subs);
    QAction *show = menuAction(subs, QStringLiteral("Show Subtitles"));
    QVERIFY(show);
    press(Qt::Key_H, Qt::AltModifier);
    QTRY_VERIFY(!prop("sub-visibility").toBool());
    Q_EMIT m_window->findChild<PlayerMenu *>()->aboutToShow();
    QVERIFY(!show->isChecked());
    press(Qt::Key_H, Qt::AltModifier);
    QTRY_VERIFY(prop("sub-visibility").toBool());
    Q_EMIT m_window->findChild<PlayerMenu *>()->aboutToShow();
    QVERIFY(show->isChecked());
    // The menu item does the same.
    show->trigger();
    QTRY_VERIFY(!prop("sub-visibility").toBool());
    show->trigger();
    QTRY_VERIFY(prop("sub-visibility").toBool());

    // Picking Off and a track in the track menu.
    QMenu *tracks = menuAction(subs, QStringLiteral("Subtitle Track"))->menu();
    menuAction(tracks, QStringLiteral("Off"))->trigger();
    QTRY_COMPARE(propString("sid"), QStringLiteral("no"));
    QTRY_COMPARE(propString("sub-text"), QString());
    QAction *first = nullptr;
    Q_EMIT tracks->aboutToShow();
    for (QAction *action : tracks->actions()) {
        if (action->text().startsWith(QLatin1String("#1")))
            first = action;
    }
    QVERIFY(first);
    first->trigger();
    QTRY_COMPARE(propString("sid"), QStringLiteral("1"));
    QTRY_COMPARE(propString("sub-text"), QStringLiteral("Hello from the mock server"));

    // A second track as secondary subtitles, shown and hidden on their own.
    m_window->loadSubtitle(m_srt2);
    QTRY_COMPARE(propString("sid"), QStringLiteral("2"));
    m_mpv->setMpvProperty(QStringLiteral("secondary-sid"), QStringLiteral("1"));
    QTRY_COMPARE(propString("secondary-sid"), QStringLiteral("1"));
    press(Qt::Key_H, Qt::AltModifier | Qt::ShiftModifier);
    QTRY_VERIFY(!prop("secondary-sub-visibility").toBool());
    QVERIFY(prop("sub-visibility").toBool());
    press(Qt::Key_H, Qt::AltModifier | Qt::ShiftModifier);
    QTRY_VERIFY(prop("secondary-sub-visibility").toBool());
}

void SubtitleTest::dialogSearchesForPlayingFile()
{
    SubtitleDownloadDialog *dialog = openDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("SubtitleQuery"))->text(), QStringLiteral("The Matrix"));
    QCOMPARE(dialog->findChild<QComboBox *>(QStringLiteral("SubtitleLanguage"))->currentData().toString(), QStringLiteral("en"));
    QVERIFY(dialog->findChild<QCheckBox *>(QStringLiteral("SubtitleMatchHash"))->isChecked());
    QCOMPARE(dialog->movieHash(), SubtitleSearch::movieHash(m_video));
    QCOMPARE(dialog->movieHash().size(), 16);

    // One search, with the parameters in alphabetical order and the API headers.
    QCOMPARE(m_server.requests.size(), 1);
    const MockServer::Request &request = m_server.requests.first();
    QCOMPARE(request.method, QByteArray("GET"));
    QCOMPARE(request.url.path(), QStringLiteral("/api/v1/subtitles"));
    const QUrlQuery query(request.url);
    QStringList keys;
    for (const auto &item : query.queryItems())
        keys.append(item.first);
    QCOMPARE(keys, (QStringList{QStringLiteral("languages"), QStringLiteral("moviehash"), QStringLiteral("query"),
                                QStringLiteral("year")}));
    QCOMPARE(query.queryItemValue(QStringLiteral("languages")), QStringLiteral("en"));
    QCOMPARE(query.queryItemValue(QStringLiteral("moviehash")), dialog->movieHash());
    QCOMPARE(query.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded), QStringLiteral("the matrix"));
    QCOMPARE(query.queryItemValue(QStringLiteral("year")), QStringLiteral("1999"));
    QCOMPARE(request.headers.value("api-key"), QByteArray("test-key"));
    QCOMPARE(request.headers.value("user-agent"), QByteArray("PotPlayerLinux v" APP_VERSION));

    // The exact (hash) match comes first, then by downloads.
    auto *table = dialog->findChild<QTreeWidget *>(QStringLiteral("SubtitleResults"));
    QCOMPARE(table->topLevelItemCount(), 3);
    QStringList headers;
    for (int column = 0; column < table->columnCount(); ++column)
        headers.append(table->headerItem()->text(column));
    QCOMPARE(headers, (QStringList{QStringLiteral("Language"), QStringLiteral("Subtitle File Name"), QStringLiteral("Downloads"),
                                   QStringLiteral("Rating"), QStringLiteral("Format"), QStringLiteral("HI")}));
    QTreeWidgetItem *top = table->topLevelItem(0);
    QCOMPARE(top->text(1), QStringLiteral("The.Matrix.1999.1080p.BluRay.x264-GRP.srt"));
    QCOMPARE(top->data(1, Qt::UserRole + 2).toString(), QStringLiteral("Exact match"));
    QCOMPARE(top->text(0), QStringLiteral("English"));
    QCOMPARE(top->text(3), QStringLiteral("9.1"));
    QCOMPARE(table->topLevelItem(1)->text(1), QStringLiteral("The.Matrix.1999.Popular.srt"));
    QTreeWidgetItem *styled = table->topLevelItem(2);
    QCOMPARE(styled->text(4), QStringLiteral(".ass"));
    QCOMPARE(styled->data(5, Qt::UserRole + 2).toString(), QStringLiteral("HI"));
    QCOMPARE(styled->text(3), QStringLiteral("–"));

    // Downloads sort by number, not text.
    table->sortByColumn(2, Qt::DescendingOrder);
    QCOMPARE(table->topLevelItem(0)->text(1), QStringLiteral("The.Matrix.1999.Popular.srt"));
    QCOMPARE(table->topLevelItem(2)->text(1), QStringLiteral("The.Matrix.1999.1080p.BluRay.x264-GRP.srt"));
}

void SubtitleTest::downloadAndApply()
{
    // Hidden subtitles come back for a subtitle the user just downloaded.
    m_mpv->setMpvProperty(QStringLiteral("sub-visibility"), QStringLiteral("no"));
    SubtitleDownloadDialog *dialog = openDialog();
    QVERIFY(dialog);
    auto *table = dialog->findChild<QTreeWidget *>(QStringLiteral("SubtitleResults"));
    table->setCurrentItem(table->topLevelItem(2)); // the .ass one
    m_server.requests.clear();
    QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("SubtitleDownloadButton")), Qt::LeftButton);

    // The dialog closes once the file is saved next to the video and loaded.
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->findChild<SubtitleDownloadDialog *>(), 10000);
    QCOMPARE(m_server.requests.size(), 2);
    QCOMPARE(m_server.requests[0].method, QByteArray("POST"));
    QCOMPARE(QJsonDocument::fromJson(m_server.requests[0].body).object().value(QStringLiteral("file_id")).toInt(), 102);
    QCOMPARE(m_server.requests[0].headers.value("api-key"), QByteArray("test-key"));
    QCOMPARE(m_server.requests[1].url.path(), QStringLiteral("/files/102"));
    QVERIFY(!m_server.requests[1].headers.contains("api-key")); // not sent to the file server

    const QString saved = m_dir.filePath(QStringLiteral("movies/The.Matrix.1999.1080p.BluRay.x264-GRP.en.ass"));
    QVERIFY(QFileInfo::exists(saved));
    QFile file(saved);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), kSrt);

    // Loaded, selected and shown.
    QTRY_VERIFY(propString("current-tracks/sub/external-filename").endsWith(QLatin1String(".en.ass")));
    QTRY_VERIFY(prop("sub-visibility").toBool());
    QTRY_COMPARE(propString("sub-text"), QStringLiteral("Hello from the mock server"));
    auto *osd = m_window->findChild<OsdWidget *>();
    QVERIFY(osd->isVisible());
    QCOMPARE(osd->text(), QStringLiteral("Subtitle loaded: The.Matrix.1999.1080p.BluRay.x264-GRP.en.ass"));

    // A second download doesn't overwrite the first.
    dialog = openDialog();
    QVERIFY(dialog);
    table = dialog->findChild<QTreeWidget *>(QStringLiteral("SubtitleResults"));
    table->setCurrentItem(table->topLevelItem(2));
    dialog->downloadSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->findChild<SubtitleDownloadDialog *>(), 10000);
    QVERIFY(QFileInfo::exists(m_dir.filePath(QStringLiteral("movies/The.Matrix.1999.1080p.BluRay.x264-GRP.en.2.ass"))));
}

void SubtitleTest::downloadToCache()
{
    SubtitleSearch::setSaveBesideVideo(false);
    SubtitleDownloadDialog *dialog = openDialog();
    QVERIFY(dialog);
    // Double-clicking (activating) a row downloads it.
    auto *table = dialog->findChild<QTreeWidget *>(QStringLiteral("SubtitleResults"));
    Q_EMIT table->itemActivated(table->topLevelItem(0), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->findChild<SubtitleDownloadDialog *>(), 10000);
    const QString cached = SubtitleSearch::cacheDir() + QStringLiteral("/The.Matrix.1999.1080p.BluRay.x264-GRP.en.srt");
    QVERIFY(cached.startsWith(qEnvironmentVariable("XDG_CACHE_HOME")));
    QVERIFY(QFileInfo::exists(cached));
    QTRY_COMPARE(propString("current-tracks/sub/external-filename"), cached);
    QFile::remove(cached);
}

void SubtitleTest::manualSearch()
{
    SubtitleDownloadDialog *dialog = openDialog();
    QVERIFY(dialog);
    m_server.requests.clear();
    auto *query = dialog->findChild<QLineEdit *>(QStringLiteral("SubtitleQuery"));
    query->setFocus();
    query->selectAll();
    QTest::keyClicks(query, QStringLiteral("Some Other Film"));
    dialog->findChild<QCheckBox *>(QStringLiteral("SubtitleMatchHash"))->setChecked(false);
    auto *language = dialog->findChild<QComboBox *>(QStringLiteral("SubtitleLanguage"));
    language->setCurrentIndex(language->findData(QStringLiteral("fr")));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isBusy() && m_server.requests.size() == 1, 10000);

    // The file's year and hash don't apply to a title the user typed.
    const QUrlQuery sent(m_server.requests.first().url);
    QCOMPARE(sent.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded), QStringLiteral("some other film"));
    QCOMPARE(sent.queryItemValue(QStringLiteral("languages")), QStringLiteral("fr"));
    QVERIFY(!sent.hasQueryItem(QStringLiteral("year")));
    QVERIFY(!sent.hasQueryItem(QStringLiteral("moviehash")));
    // The language is remembered.
    QCOMPARE(SubtitleSearch::language(), QStringLiteral("fr"));

    // "All Languages" leaves the filter out.
    language->setCurrentIndex(language->findData(QString()));
    m_server.requests.clear();
    dialog->search();
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isBusy() && m_server.requests.size() == 1, 10000);
    QVERIFY(!QUrlQuery(m_server.requests.first().url).hasQueryItem(QStringLiteral("languages")));
}

void SubtitleTest::queryEncoding()
{
    SubtitleDownloadDialog *dialog = openDialog();
    QVERIFY(dialog);
    m_server.requests.clear();
    dialog->findChild<QLineEdit *>(QStringLiteral("SubtitleQuery"))->setText(QStringLiteral("Tom & Jerry + Friends Été 100%"));
    dialog->search();
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isBusy() && m_server.requests.size() == 1, 10000);
    const QUrlQuery sent(m_server.requests.first().url);
    QCOMPARE(sent.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded), QStringLiteral("tom & jerry + friends été 100%"));
}

void SubtitleTest::busyAndCancel()
{
    bool hold = false;
    m_server.handler = [&](const MockServer::Request &request) {
        MockServer::Response response = defaultResponse(request);
        response.hold = hold;
        return response;
    };
    SubtitleDownloadDialog *dialog = openDialog();
    QVERIFY(dialog);

    // While a search runs, the bar spins and the inputs wait.
    hold = true;
    dialog->search();
    auto *progress = dialog->findChild<QProgressBar *>(QStringLiteral("SubtitleProgress"));
    auto *searchButton = dialog->findChild<QPushButton *>(QStringLiteral("SubtitleSearchButton"));
    QVERIFY(dialog->isBusy());
    QVERIFY(progress->isVisible());
    QCOMPARE(progress->maximum(), 0); // indeterminate
    QVERIFY(!searchButton->isEnabled());
    QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("SubtitleDownloadButton"))->isEnabled());
    // The player keeps running meanwhile.
    QVERIFY(m_window->isEnabled());

    // Esc stops the request first and keeps the dialog open; a second Esc closes it.
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(!dialog->isBusy());
    QVERIFY(!progress->isVisible());
    QVERIFY(searchButton->isEnabled());
    QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("SubtitleStatus"))->text(), QStringLiteral("Cancelled."));
    QVERIFY(dialog->isVisible());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(!m_window->findChild<SubtitleDownloadDialog *>());
}

void SubtitleTest::apiErrors()
{
    m_server.handler = [](const MockServer::Request &request) -> MockServer::Response {
        if (request.method == "POST")
            return {406, "{\"message\":\"You have downloaded your allowed 5 subtitles for 24h\"}"};
        return {401, "{\"message\":\"You cannot consume this service\"}"};
    };
    press(Qt::Key_D);
    SubtitleDownloadDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = m_window->findChild<SubtitleDownloadDialog *>()));
    auto *status = dialog->findChild<QLabel *>(QStringLiteral("SubtitleStatus"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isBusy() && status->text().contains(QLatin1String("You cannot consume")), 10000);
    QVERIFY(status->text().startsWith(QLatin1String("OpenSubtitles refused the request")));
    QVERIFY(!dialog->findChild<QProgressBar *>(QStringLiteral("SubtitleProgress"))->isVisible());

    // A failed download reports the API's message and leaves the dialog open.
    OpenSubtitlesClient::Result result;
    result.fileId = 1;
    QSignalSpy failed(dialog->client(), &OpenSubtitlesClient::failed);
    dialog->client()->download(result, m_dir.filePath(QStringLiteral("never.srt")));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("You have downloaded your allowed 5 subtitles for 24h"));
    QVERIFY(!QFileInfo::exists(m_dir.filePath(QStringLiteral("never.srt"))));
}

void SubtitleTest::missingApiKey()
{
    SubtitleSearch::setUserApiKey(QString());
    if (!SubtitleSearch::builtInApiKey().isEmpty())
        QSKIP("This build has a built-in API key");
    press(Qt::Key_D);
    SubtitleDownloadDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = m_window->findChild<SubtitleDownloadDialog *>()));
    QTest::qWait(100);
    QVERIFY(m_server.requests.isEmpty());
    auto *status = dialog->findChild<QLabel *>(QStringLiteral("SubtitleStatus"));
    QVERIFY(status->text().contains(QLatin1String("API key")));

    // Entering one in Settings searches right away.
    QTimer::singleShot(0, [] {
        QTRY_VERIFY(qobject_cast<SubtitleSettingsDialog *>(QApplication::activeModalWidget()));
        auto *settings = qobject_cast<SubtitleSettingsDialog *>(QApplication::activeModalWidget());
        settings->findChild<QLineEdit *>(QStringLiteral("ApiKeyEdit"))->setText(QStringLiteral("  new-key  "));
        settings->accept();
    });
    QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("SubtitleSettingsButton")), Qt::LeftButton);
    QCOMPARE(SubtitleSearch::userApiKey(), QStringLiteral("new-key"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isBusy() && m_server.requests.size() == 1, 10000);
    QCOMPARE(m_server.requests.first().headers.value("api-key"), QByteArray("new-key"));
}

int main(int argc, char *argv[])
{
    // Keep the settings and downloads away from the user's own.
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", (config.path() + QStringLiteral("/config")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", (config.path() + QStringLiteral("/cache")).toLocal8Bit());
    QApplication app(argc, argv);
    // libmpv requires the C numeric locale; QApplication may have changed it.
    std::setlocale(LC_NUMERIC, "C");
    SubtitleTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_subtitles.moc"
