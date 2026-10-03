#include "StreamCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int kTransferTimeoutMs = 20000;
const QByteArray kUserAgent = QByteArrayLiteral("PotPlayerLinux/" APP_VERSION);

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

} // namespace

namespace StreamCatalog {

QList<Country> countries()
{
    QList<Country> list{
        {QStringLiteral("ar"), QObject::tr("Argentina")},
        {QStringLiteral("au"), QObject::tr("Australia")},
        {QStringLiteral("br"), QObject::tr("Brazil")},
        {QStringLiteral("ca"), QObject::tr("Canada")},
        {QStringLiteral("cn"), QObject::tr("China")},
        {QStringLiteral("dj"), QObject::tr("Djibouti")},
        {QStringLiteral("eg"), QObject::tr("Egypt")},
        {QStringLiteral("er"), QObject::tr("Eritrea")},
        {QStringLiteral("fr"), QObject::tr("France")},
        {QStringLiteral("de"), QObject::tr("Germany")},
        {QStringLiteral("in"), QObject::tr("India")},
        {QStringLiteral("it"), QObject::tr("Italy")},
        {QStringLiteral("jp"), QObject::tr("Japan")},
        {QStringLiteral("ke"), QObject::tr("Kenya")},
        {QStringLiteral("mx"), QObject::tr("Mexico")},
        {QStringLiteral("nl"), QObject::tr("Netherlands")},
        {QStringLiteral("ng"), QObject::tr("Nigeria")},
        {QStringLiteral("pt"), QObject::tr("Portugal")},
        {QStringLiteral("ru"), QObject::tr("Russia")},
        {QStringLiteral("sa"), QObject::tr("Saudi Arabia")},
        {QStringLiteral("so"), QObject::tr("Somalia")},
        {QStringLiteral("za"), QObject::tr("South Africa")},
        {QStringLiteral("kr"), QObject::tr("South Korea")},
        {QStringLiteral("es"), QObject::tr("Spain")},
        {QStringLiteral("sd"), QObject::tr("Sudan")},
        {QStringLiteral("se"), QObject::tr("Sweden")},
        {QStringLiteral("tr"), QObject::tr("Turkey")},
        {QStringLiteral("ae"), QObject::tr("United Arab Emirates")},
        {QStringLiteral("gb"), QObject::tr("United Kingdom")},
        {QStringLiteral("us"), QObject::tr("United States")},
    };
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
            if (pending.name.isEmpty())
                pending.name = attributes.value(QStringLiteral("tvg-name"));
            const QRegularExpressionMatch quality = qualityPattern.match(pending.name);
            if (quality.hasMatch())
                pending.quality = quality.captured(1);
            hasInfo = true;
        } else if (!line.startsWith(QLatin1Char('#'))) {
            if (!hasInfo)
                pending = Station();
            pending.url = line;
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
        QStringList tags = object.value(QStringLiteral("tags")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString &tag : tags)
            tag = tag.trimmed();
        tags.removeAll(QString());
        station.genre = tags.join(QStringLiteral(", "));
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

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/potplayer-linux/streams");
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
