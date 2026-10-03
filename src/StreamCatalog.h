#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

// Live TV channels from the iptv-org playlists and radio stations from the
// Radio-Browser community directory: where to get them, how to read them, and
// the cache in ~/.cache/potplayer-linux/streams/.
namespace StreamCatalog {

struct Station {
    QString name;
    QString url;
    QString logo;     // image URL, may be empty
    QString country;
    QString genre;    // M3U group-title, or Radio-Browser tags
    QString language;
    QString quality;  // e.g. "720p" from a channel name
    int bitrate = 0;  // kbit/s, 0 if unknown
};

struct Country {
    QString code; // ISO 3166-1 alpha-2, lower case
    QString name;
};

// The countries offered for browsing: Ethiopia first, the rest by name.
QList<Country> countries();

QUrl tvCountryUrl(const QString &code);
QUrl tvCategoryIndexUrl();
QUrl radioCountryUrl(const QString &code);

// Channels of an extended M3U playlist (#EXTINF lines with tvg-logo,
// group-title, tvg-language and tvg-country attributes).
QList<Station> parseM3u(const QByteArray &data);
// Stations of a Radio-Browser JSON array.
QList<Station> parseRadioBrowser(const QByteArray &data);

// True if every word of `filter` is in the station's name, genre, language or country.
bool matches(const Station &station, const QString &filter);

// ~/.cache/potplayer-linux/streams (follows $XDG_CACHE_HOME).
QString cacheDir();
QString cacheFile(const QString &key);
// Cached copies are used without asking the server for this long.
constexpr qint64 kCacheMaxAgeSecs = 24 * 60 * 60;
bool isCacheFresh(const QString &key);

} // namespace StreamCatalog

// Downloads catalog files and keeps them in the cache. Fresh cached copies
// are returned without a request; when the server can't be reached, a stale
// copy is used instead of failing.
class StreamFetcher : public QObject
{
    Q_OBJECT

public:
    explicit StreamFetcher(QObject *parent = nullptr);

    QNetworkAccessManager *network() const { return m_network; }
    // Fetches `url`, cached as `key`. Answers with loaded() or failed(), never synchronously.
    void fetch(const QString &key, const QUrl &url, bool refresh = false);

Q_SIGNALS:
    void loaded(const QString &key, const QByteArray &data, const QDateTime &fetched, bool fromCache);
    void failed(const QString &key, const QString &error);

private:
    void loadCached(const QString &key, const QString &error);

    QNetworkAccessManager *m_network;
};
