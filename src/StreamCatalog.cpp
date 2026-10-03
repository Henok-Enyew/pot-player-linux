#include "StreamCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int kTransferTimeoutMs = 20000;
const QByteArray kUserAgent = QByteArrayLiteral("TopPlayer/" APP_VERSION);

// Reads the key="value" attributes of an #EXTINF line up to the comma that
// starts the title, which may itself contain commas.
QString parseExtinf(const QString &line, QHash<QString, QString> *attributes)
{
    qsizetype i = line.indexOf(QLatin1Char(':')) + 1;
    bool quoted = false;
    qsizetype keyStart = -1;
    QString key;
    qsizetype valueStart = -1;
    for (; i < line.size(); ++i) {
        const QChar c = line[i];
        if (quoted) {
            if (c == QLatin1Char('"')) {
                attributes->insert(key.toLower(), line.mid(valueStart, i - valueStart).trimmed());
                quoted = false;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
            valueStart = i + 1;
        } else if (c == QLatin1Char('=')) {
            key = keyStart >= 0 ? line.mid(keyStart, i - keyStart).trimmed() : QString();
        } else if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            keyStart = i + 1;
        } else if (c == QLatin1Char(',')) {
            return line.mid(i + 1).trimmed();
        }
    }
    return {};
}

// The country code of an iptv-org channel ID: "EBS.us@HD" -> "us".
QString countryOfId(const QString &id)
{
    const QString channel = id.section(QLatin1Char('@'), 0, 0);
    const QString code = channel.section(QLatin1Char('.'), -1);
    return channel.contains(QLatin1Char('.')) && code.size() == 2 ? code.toLower() : QString();
}

// Splits "a, b" genre lists.
QStringList splitGenres(const QString &genres)
{
    QStringList list = genres.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &genre : list)
        genre = genre.trimmed();
    list.removeAll(QString());
    return list;
}

} // namespace

namespace StreamCatalog {

const QString kBrowserUserAgent = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36");

bool Station::isGeoBlocked() const
{
    return name.contains(QLatin1String("[Geo-blocked]"), Qt::CaseInsensitive);
}

QVariantMap playbackOptions(const Station &station)
{
    QVariantMap options{{QStringLiteral("force-media-title"), station.name}};
    // Radio servers are left with mpv's own user agent: some SHOUTcast
    // servers answer browsers with a web page instead of the stream.
    const QString userAgent = !station.userAgent.isEmpty() ? station.userAgent : station.tv ? kBrowserUserAgent : QString();
    if (!userAgent.isEmpty())
        options.insert(QStringLiteral("user-agent"), userAgent);
    if (!station.referrer.isEmpty()) {
        options.insert(QStringLiteral("referrer"), station.referrer);
        // Servers that check the referrer usually check the origin too.
        const QUrl referrer(station.referrer);
        if (referrer.isValid() && !referrer.host().isEmpty()) {
            options.insert(QStringLiteral("http-header-fields"),
                           QStringLiteral("Origin: %1").arg(referrer.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString()));
        }
    }
    // These are direct media URLs. Without this, a dead stream is handed to
    // yt-dlp next, which can only fail again, slowly.
    const QString host = QUrl(station.url).host();
    if (!host.contains(QLatin1String("youtube.")) && !host.contains(QLatin1String("youtu.be")))
        options.insert(QStringLiteral("ytdl"), QStringLiteral("no"));
    // Give up on unreachable servers after 15 s instead of a minute.
    options.insert(QStringLiteral("network-timeout"), QStringLiteral("15"));
    return options;
}

QString countryName(const QString &code)
{
    if (code.size() != 2)
        return {};
    const QLocale::Territory territory = QLocale::codeToTerritory(code.toUpper());
    return territory == QLocale::AnyTerritory ? QString() : QLocale::territoryToString(territory);
}

QList<Country> countries()
{
    // Every country Qt knows by its two-letter code (numeric codes are regions such as "Latin America").
    QList<Country> list;
    for (int i = QLocale::AnyTerritory + 1; i <= QLocale::LastTerritory; ++i) {
        const auto territory = static_cast<QLocale::Territory>(i);
        const QString code = QLocale::territoryToCode(territory).toLower();
        static const QStringList kNotCountries{QStringLiteral("eu"), QStringLiteral("ez"), QStringLiteral("un"), QStringLiteral("qo")};
        if (code.size() != 2 || !code[0].isLetter() || code == QLatin1String("et") || kNotCountries.contains(code))
            continue;
        if (std::any_of(list.cbegin(), list.cend(), [&code](const Country &c) { return c.code == code; }))
            continue; // aliases of the same territory
        list.append({code, QLocale::territoryToString(territory)});
    }
    // Sorted in the user's language.
    std::sort(list.begin(), list.end(), [](const Country &a, const Country &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    list.prepend({QStringLiteral("et"), QObject::tr("Ethiopia")});
    return list;
}

QUrl tvCountryUrl(const QString &code)
{
    return QUrl(QStringLiteral("https://iptv-org.github.io/iptv/countries/%1.m3u").arg(code.toLower()));
}

QUrl tvCategoryIndexUrl()
{
    return QUrl(QStringLiteral("https://iptv-org.github.io/iptv/index.category.m3u"));
}

QUrl radioCountryUrl(const QString &code)
{
    // By country code: Radio-Browser's country names ("The United States Of
    // America") don't match common names reliably.
    return QUrl(QStringLiteral("https://de1.api.radio-browser.info/json/stations/bycountrycodeexact/%1"
                               "?hidebroken=true&order=clickcount&reverse=true&limit=1000")
                    .arg(code.toUpper()));
}

QList<Station> parseM3u(const QByteArray &data)
{
    static const QRegularExpression qualityPattern(QStringLiteral("\\((\\d{3,4}[pi])\\)"));
    QList<Station> stations;
    Station pending;
    bool hasInfo = false;
    for (const QString &rawLine : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("#EXTINF"), Qt::CaseInsensitive)) {
            QHash<QString, QString> attributes;
            pending = Station();
            pending.name = parseExtinf(line, &attributes);
            pending.logo = attributes.value(QStringLiteral("tvg-logo"));
            pending.genre = attributes.value(QStringLiteral("group-title")).replace(QLatin1Char(';'), QStringLiteral(", "));
            pending.language = attributes.value(QStringLiteral("tvg-language")).replace(QLatin1Char(';'), QStringLiteral(", "));
            pending.country = attributes.value(QStringLiteral("tvg-country")).replace(QLatin1Char(';'), QStringLiteral(", "));
            pending.id = attributes.value(QStringLiteral("tvg-id"));
            if (pending.country.isEmpty())
                pending.country = countryName(countryOfId(pending.id));
            pending.referrer = attributes.value(QStringLiteral("http-referrer"));
            pending.userAgent = attributes.value(QStringLiteral("http-user-agent"));
            if (pending.name.isEmpty())
                pending.name = attributes.value(QStringLiteral("tvg-name"));
            const QRegularExpressionMatch quality = qualityPattern.match(pending.name);
            if (quality.hasMatch())
                pending.quality = quality.captured(1);
            hasInfo = true;
        } else if (line.startsWith(QLatin1String("#EXTVLCOPT:"), Qt::CaseInsensitive)) {
            // Options for the next URL, as VLC reads them.
            const QString option = line.mid(11).trimmed();
            const QString key = option.section(QLatin1Char('='), 0, 0).trimmed().toLower();
            const QString value = option.section(QLatin1Char('='), 1).trimmed();
            if (key == QLatin1String("http-referrer") || key == QLatin1String("http-referer"))
                pending.referrer = value;
            else if (key == QLatin1String("http-user-agent"))
                pending.userAgent = value;
        } else if (!line.startsWith(QLatin1Char('#'))) {
            if (!hasInfo) {
                // Keeps options given before a bare URL.
                const QString referrer = pending.referrer;
                const QString userAgent = pending.userAgent;
                pending = Station();
                pending.referrer = referrer;
                pending.userAgent = userAgent;
            }
            pending.url = line;
            pending.tv = true;
            if (pending.name.isEmpty())
                pending.name = line;
            stations.append(pending);
            pending = Station();
            hasInfo = false;
        }
    }
    return stations;
}

QList<Station> parseRadioBrowser(const QByteArray &data)
{
    QList<Station> stations;
    for (const QJsonValue &value : QJsonDocument::fromJson(data).array()) {
        const QJsonObject object = value.toObject();
        Station station;
        station.name = object.value(QStringLiteral("name")).toString().trimmed();
        station.url = object.value(QStringLiteral("url_resolved")).toString().trimmed();
        if (station.url.isEmpty())
            station.url = object.value(QStringLiteral("url")).toString().trimmed();
        if (station.url.isEmpty())
            continue;
        if (station.name.isEmpty())
            station.name = station.url;
        station.logo = object.value(QStringLiteral("favicon")).toString().trimmed();
        station.country = object.value(QStringLiteral("country")).toString();
        station.language = object.value(QStringLiteral("language")).toString().replace(QLatin1Char(','), QStringLiteral(", "));
        station.genre = splitGenres(object.value(QStringLiteral("tags")).toString()).join(QStringLiteral(", "));
        station.bitrate = object.value(QStringLiteral("bitrate")).toInt();
        stations.append(station);
    }
    return stations;
}

bool matches(const Station &station, const QString &filter)
{
    const QStringList words = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(words.cbegin(), words.cend(), [&station](const QString &word) {
        for (const QString *field : {&station.name, &station.genre, &station.language, &station.country}) {
            if (field->contains(word, Qt::CaseInsensitive))
                return true;
        }
        return false;
    });
}

QStringList genres(const QList<Station> &stations, int limit)
{
    // Counted case-insensitively, shown as most stations spell them.
    QHash<QString, int> counts;
    QHash<QString, QHash<QString, int>> spellings;
    for (const Station &station : stations) {
        for (const QString &genre : splitGenres(station.genre)) {
            const QString key = genre.toLower();
            ++counts[key];
            ++spellings[key][genre];
        }
    }
    QStringList keys = counts.keys();
    std::sort(keys.begin(), keys.end(), [&counts](const QString &a, const QString &b) {
        return counts[a] != counts[b] ? counts[a] > counts[b] : a < b;
    });
    if (limit >= 0 && keys.size() > limit)
        keys.resize(limit);
    QStringList result;
    for (const QString &key : std::as_const(keys)) {
        const QHash<QString, int> &forms = spellings[key];
        result.append(std::max_element(forms.cbegin(), forms.cend()).key());
    }
    return result;
}

bool hasGenre(const Station &station, const QString &genre)
{
    if (genre.isEmpty())
        return true;
    const QStringList list = splitGenres(station.genre);
    return std::any_of(list.cbegin(), list.cend(), [&genre](const QString &g) { return g.compare(genre, Qt::CaseInsensitive) == 0; });
}

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/top-player/streams");
}

QString cacheFile(const QString &key)
{
    return cacheDir() + QLatin1Char('/') + key;
}

bool isCacheFresh(const QString &key)
{
    const QFileInfo info(cacheFile(key));
    return info.isFile() && info.lastModified().secsTo(QDateTime::currentDateTime()) < kCacheMaxAgeSecs;
}

} // namespace StreamCatalog

StreamFetcher::StreamFetcher(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void StreamFetcher::fetch(const QString &key, const QUrl &url, bool refresh)
{
    if (!refresh && StreamCatalog::isCacheFresh(key)) {
        QTimer::singleShot(0, this, [this, key] { loadCached(key, QString()); });
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            loadCached(key, reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        QDir().mkpath(StreamCatalog::cacheDir());
        QSaveFile file(StreamCatalog::cacheFile(key));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.commit();
        }
        Q_EMIT loaded(key, data, QDateTime::currentDateTime(), false);
    });
}

void StreamFetcher::loadCached(const QString &key, const QString &error)
{
    QFile file(StreamCatalog::cacheFile(key));
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT failed(key, error.isEmpty() ? file.errorString() : error);
        return;
    }
    Q_EMIT loaded(key, file.readAll(), QFileInfo(file).lastModified(), true);
}
