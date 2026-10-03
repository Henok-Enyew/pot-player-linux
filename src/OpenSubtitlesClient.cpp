#include "OpenSubtitlesClient.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

#include <algorithm>

namespace {

constexpr int kTimeoutMs = 30000;
const QStringList kSubtitleFormats{
    QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"), QStringLiteral("vtt"),
    QStringLiteral("sub"), QStringLiteral("smi"), QStringLiteral("txt"),
};

QString formatOf(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return kSubtitleFormats.contains(suffix) ? suffix : QStringLiteral("srt");
}

} // namespace

OpenSubtitlesClient::OpenSubtitlesClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(QStringLiteral("https://api.opensubtitles.com/api/v1"))
{
    const QString override = qEnvironmentVariable("TOPPLAYER_OPENSUBTITLES_URL");
    if (!override.isEmpty())
        m_baseUrl = QUrl(override);
}

QNetworkRequest OpenSubtitlesClient::apiRequest(const QString &endpoint, const QUrlQuery &query) const
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + endpoint);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Api-Key", m_apiKey.toUtf8());
    request.setRawHeader("User-Agent", SubtitleSearch::userAgent());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

void OpenSubtitlesClient::track(QNetworkReply *reply)
{
    cancel();
    m_reply = reply;
}

void OpenSubtitlesClient::cancel()
{
    if (QNetworkReply *reply = m_reply.data()) {
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void OpenSubtitlesClient::search(const Query &query)
{
    // The API asks for parameters in alphabetical order with lower-case
    // values; anything else costs a redirect.
    QList<std::pair<QString, QString>> items;
    if (query.episode >= 0)
        items.append({QStringLiteral("episode_number"), QString::number(query.episode)});
    if (!query.languages.isEmpty()) {
        QStringList codes;
        for (const QString &code : query.languages)
            codes.append(code.toLower());
        codes.sort();
        items.append({QStringLiteral("languages"), codes.join(QLatin1Char(','))});
    }
    if (!query.movieHash.isEmpty())
        items.append({QStringLiteral("moviehash"), query.movieHash.toLower()});
    if (!query.text.trimmed().isEmpty())
        items.append({QStringLiteral("query"), query.text.trimmed().toLower()});
    if (query.season >= 0)
        items.append({QStringLiteral("season_number"), QString::number(query.season)});
    if (query.year > 0)
        items.append({QStringLiteral("year"), QString::number(query.year)});
    std::sort(items.begin(), items.end());
    QUrlQuery urlQuery;
    for (const auto &[key, value] : items) {
        // QUrlQuery escapes '&' and '=', but '+' would read as a space.
        QString escaped = value;
        escaped.replace(QLatin1Char('%'), QLatin1String("%25")).replace(QLatin1Char('+'), QLatin1String("%2B"));
        urlQuery.addQueryItem(key, escaped);
    }

    QNetworkReply *reply = m_network->get(apiRequest(QStringLiteral("/subtitles"), urlQuery));
    track(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_reply = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT failed(errorMessage(reply, body));
            return;
        }
        Q_EMIT searchFinished(parseResults(body));
    });
}

QList<OpenSubtitlesClient::Result> OpenSubtitlesClient::parseResults(const QByteArray &json)
{
    QList<Result> results;
    const QJsonArray data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue &value : data) {
        const QJsonObject attributes = value.toObject().value(QStringLiteral("attributes")).toObject();
        const QJsonArray files = attributes.value(QStringLiteral("files")).toArray();
        // A subtitle in several parts (CD1, CD2) has one file per part;
        // the first is the one for a single-file video.
        if (files.isEmpty())
            continue;
        const QJsonObject file = files.first().toObject();
        Result result;
        result.provider = QStringLiteral("OpenSubtitles");
        result.fileId = file.value(QStringLiteral("file_id")).toInt();
        if (result.fileId == 0)
            continue;
        result.id = QString::number(result.fileId);
        result.fileName = file.value(QStringLiteral("file_name")).toString();
        result.release = attributes.value(QStringLiteral("release")).toString();
        if (result.fileName.isEmpty())
            result.fileName = result.release;
        result.language = attributes.value(QStringLiteral("language")).toString();
        result.downloads = attributes.value(QStringLiteral("download_count")).toInteger();
        result.rating = attributes.value(QStringLiteral("ratings")).toDouble();
        result.format = formatOf(result.fileName);
        result.hearingImpaired = attributes.value(QStringLiteral("hearing_impaired")).toBool();
        result.hashMatch = attributes.value(QStringLiteral("moviehash_match")).toBool();
        result.machineTranslated = attributes.value(QStringLiteral("machine_translated")).toBool()
            || attributes.value(QStringLiteral("ai_translated")).toBool();
        result.uploader = attributes.value(QStringLiteral("uploader")).toObject().value(QStringLiteral("name")).toString();
        results.append(result);
    }
    // Exact matches first, then the most downloaded.
    std::stable_sort(results.begin(), results.end(), [](const Result &a, const Result &b) {
        if (a.hashMatch != b.hashMatch)
            return a.hashMatch;
        return a.downloads > b.downloads;
    });
    return results;
}

void OpenSubtitlesClient::download(const Result &result, const QString &path)
{
    // Asking for a download returns a temporary link to the file.
    const QJsonObject body{{QStringLiteral("file_id"), result.fileId}};
    QNetworkReply *reply = m_network->post(apiRequest(QStringLiteral("/download")),
                                           QJsonDocument(body).toJson(QJsonDocument::Compact));
    track(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        m_reply = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT failed(errorMessage(reply, body));
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        const QUrl link(object.value(QStringLiteral("link")).toString());
        if (!link.isValid() || link.isRelative()) {
            const QString message = object.value(QStringLiteral("message")).toString();
            Q_EMIT failed(message.isEmpty() ? tr("The server sent no download link.") : message);
            return;
        }
        fetchFile(link, path);
    });
}

void OpenSubtitlesClient::fetchFile(const QUrl &link, const QString &path)
{
    // The link points at a file server: no API key for it.
    QNetworkRequest request(link);
    request.setRawHeader("User-Agent", SubtitleSearch::userAgent());
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    track(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        m_reply = nullptr;
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT failed(errorMessage(reply, data));
            return;
        }
        if (data.isEmpty()) {
            Q_EMIT failed(tr("The subtitle file is empty."));
            return;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            Q_EMIT failed(tr("Could not save %1: %2").arg(path, file.errorString()));
            return;
        }
        Q_EMIT downloadFinished(path);
    });
}

QString OpenSubtitlesClient::errorMessage(QNetworkReply *reply, const QByteArray &body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    QString message = object.value(QStringLiteral("message")).toString();
    if (message.isEmpty()) {
        QStringList errors;
        for (const QJsonValue &error : object.value(QStringLiteral("errors")).toArray())
            errors.append(error.toString());
        message = errors.join(QLatin1Char(' '));
    }
    if (status == 401 || status == 403) {
        return message.isEmpty() ? tr("OpenSubtitles refused the API key. Check it in Settings.")
                                 : tr("OpenSubtitles refused the request: %1").arg(message);
    }
    if (status == 406 || status == 429)
        return message.isEmpty() ? tr("The OpenSubtitles download limit is reached; try again later.") : message;
    if (!message.isEmpty())
        return message;
    return SubtitleSearch::networkErrorMessage(QStringLiteral("OpenSubtitles.com"), reply);
}
