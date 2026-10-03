#include "PodnapisiClient.h"
#include "ZipArchive.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {

constexpr int kTimeoutMs = 20000;
const QString kService = QStringLiteral("podnapisi.net");
const QStringList kSubtitleSuffixes{
    QStringLiteral("srt"), QStringLiteral("vtt"), QStringLiteral("ass"), QStringLiteral("ssa"),
    QStringLiteral("sub"), QStringLiteral("smi"), QStringLiteral("txt"),
};

// podnapisi.net names formats ("SubRip", "MicroDVD", ...); files are .srt unless told otherwise.
QString formatFromName(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QLatin1String("advanced")) || lower == QLatin1String("ass"))
        return QStringLiteral("ass");
    if (lower.contains(QLatin1String("substation")) || lower == QLatin1String("ssa"))
        return QStringLiteral("ssa");
    if (lower.contains(QLatin1String("webvtt")) || lower == QLatin1String("vtt"))
        return QStringLiteral("vtt");
    if (lower.contains(QLatin1String("microdvd")))
        return QStringLiteral("sub");
    return QStringLiteral("srt");
}

} // namespace

PodnapisiClient::PodnapisiClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(QStringLiteral("https://www.podnapisi.net"))
{
    const QString override = qEnvironmentVariable("POTPLAYER_PODNAPISI_URL");
    if (!override.isEmpty())
        m_baseUrl = QUrl(override);
}

QString PodnapisiClient::languageCode(const QString &code)
{
    return code.toLower();
}

QNetworkRequest PodnapisiClient::request(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", SubtitleSearch::userAgent());
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

void PodnapisiClient::track(QNetworkReply *reply)
{
    cancel();
    m_reply = reply;
}

void PodnapisiClient::cancel()
{
    if (QNetworkReply *reply = m_reply.data()) {
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void PodnapisiClient::search(const SubtitleQuery &query)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/subtitles/search/old"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("sXML"), QStringLiteral("1"));
    QString keywords = query.text.trimmed();
    keywords.replace(QLatin1Char('%'), QLatin1String("%25")).replace(QLatin1Char('+'), QLatin1String("%2B"));
    params.addQueryItem(QStringLiteral("sK"), keywords);
    if (!query.languages.isEmpty())
        params.addQueryItem(QStringLiteral("sL"), languageCode(query.languages.first()));
    if (query.year > 0)
        params.addQueryItem(QStringLiteral("sY"), QString::number(query.year));
    if (query.season >= 0)
        params.addQueryItem(QStringLiteral("sTS"), QString::number(query.season));
    if (query.episode >= 0)
        params.addQueryItem(QStringLiteral("sTE"), QString::number(query.episode));
    url.setQuery(params);

    QNetworkRequest searchRequest = request(url);
    searchRequest.setRawHeader("Accept", "application/xml, text/xml");
    QNetworkReply *reply = m_network->get(searchRequest);
    track(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_reply = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT failed(SubtitleSearch::networkErrorMessage(kService, reply));
            return;
        }
        if (!body.contains("<results") && !body.contains("<subtitle")) {
            Q_EMIT failed(tr("%1 sent an answer the player doesn't understand.").arg(kService));
            return;
        }
        Q_EMIT searchFinished(parseResults(body));
    });
}

QList<SubtitleResult> PodnapisiClient::parseResults(const QByteArray &xml)
{
    QList<SubtitleResult> results;
    QXmlStreamReader reader(xml);
    SubtitleResult current;
    QString title;
    QString year;
    bool inSubtitle = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == QLatin1String("subtitle")) {
                inSubtitle = true;
                current = SubtitleResult{};
                current.provider = QStringLiteral("Podnapisi");
                title.clear();
                year.clear();
                continue;
            }
            if (!inSubtitle)
                continue;
            const QString text = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if (name == QLatin1String("pid"))
                current.id = text;
            else if (name == QLatin1String("title"))
                title = text;
            else if (name == QLatin1String("year"))
                year = text;
            else if (name == QLatin1String("url"))
                current.pageUrl = QUrl(text);
            else if (name == QLatin1String("release"))
                current.release = text.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
            else if (name == QLatin1String("language"))
                current.language = text;
            else if (name == QLatin1String("rating"))
                current.rating = text.toDouble();
            else if (name == QLatin1String("downloads"))
                current.downloads = text.toLongLong();
            else if (name == QLatin1String("format"))
                current.format = formatFromName(text);
            else if (name == QLatin1String("flags"))
                current.hearingImpaired = text.contains(QLatin1Char('n'));
            else if (name == QLatin1String("uploaderName"))
                current.uploader = text;
        } else if (reader.isEndElement() && reader.name() == QLatin1String("subtitle")) {
            inSubtitle = false;
            if (current.id.isEmpty())
                continue;
            if (current.format.isEmpty())
                current.format = QStringLiteral("srt");
            // The release name when there is one, else "Title (Year)".
            current.fileName = !current.release.isEmpty()
                ? current.release
                : (year.isEmpty() || year == QLatin1String("0") ? title : QStringLiteral("%1 (%2)").arg(title, year));
            results.append(current);
        }
    }
    return results;
}

void PodnapisiClient::download(const SubtitleResult &result, const QString &path)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/subtitles/%1/download").arg(result.id));
    url.setQuery(QStringLiteral("container=zip"));
    QNetworkRequest downloadRequest = request(url);
    if (result.pageUrl.isValid())
        downloadRequest.setRawHeader("Referer", result.pageUrl.toEncoded());
    QNetworkReply *reply = m_network->get(downloadRequest);
    track(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        m_reply = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT failed(SubtitleSearch::networkErrorMessage(kService, reply));
            return;
        }
        QByteArray data = body;
        QString target = path;
        if (ZipArchive::isZip(body)) {
            const auto entry = ZipArchive::extract(body, [](const QString &name) {
                return kSubtitleSuffixes.contains(QFileInfo(name).suffix().toLower());
            });
            if (!entry || entry->data.isEmpty()) {
                Q_EMIT failed(tr("The download from %1 has no subtitle file in it.").arg(kService));
                return;
            }
            data = entry->data;
            // Keep the format of the file in the archive.
            const QFileInfo info(path);
            target = info.dir().filePath(info.completeBaseName() + QLatin1Char('.') + QFileInfo(entry->name).suffix().toLower());
        } else if (body.isEmpty() || body.trimmed().startsWith('<')) {
            // An HTML page instead of a file.
            Q_EMIT failed(tr("%1 didn't send the subtitle file. Try another one.").arg(kService));
            return;
        }
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            Q_EMIT failed(tr("Could not save %1: %2").arg(target, file.errorString()));
            return;
        }
        Q_EMIT downloadFinished(target);
    });
}
