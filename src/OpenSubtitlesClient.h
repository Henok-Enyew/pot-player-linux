#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

// A client for the OpenSubtitles.com REST API (v1), built on Qt Network.
// Every request is asynchronous; results arrive through the signals.
class OpenSubtitlesClient : public QObject
{
    Q_OBJECT

public:
    struct Query {
        QString text;          // title to search for; may be empty with a hash
        QStringList languages; // OpenSubtitles codes ("en", "pt-BR"); empty for all
        QString movieHash;     // 16 hex digits, or empty
        int year = 0;
        int season = -1;
        int episode = -1;
    };

    struct Result {
        int fileId = 0;
        QString language;
        QString fileName;
        QString release;
        qint64 downloads = 0;
        double rating = 0;     // 0-10, 0 if not rated
        QString format;        // "srt", "ass", ...
        bool hearingImpaired = false;
        bool hashMatch = false; // made for this exact file
        bool machineTranslated = false;
        QString uploader;
    };

    explicit OpenSubtitlesClient(QObject *parent = nullptr);

    // https://api.opensubtitles.com/api/v1, or $POTPLAYER_OPENSUBTITLES_URL.
    QUrl baseUrl() const { return m_baseUrl; }
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    void setApiKey(const QString &key) { m_apiKey = key; }
    QString apiKey() const { return m_apiKey; }
    static QByteArray userAgent();

    // Searches; searchFinished() or failed() follows. Replaces a running search.
    void search(const Query &query);
    // Downloads the subtitle file to `path`; downloadFinished() or failed() follows.
    void download(const Result &result, const QString &path);
    // Abandons the running request without a signal.
    void cancel();
    bool isBusy() const { return !m_reply.isNull(); }

    // Parses a search response; public for tests.
    static QList<Result> parseResults(const QByteArray &json);

Q_SIGNALS:
    void searchFinished(const QList<OpenSubtitlesClient::Result> &results);
    void downloadFinished(const QString &path);
    void failed(const QString &message);

private:
    QNetworkRequest apiRequest(const QString &endpoint, const QUrlQuery &query = {}) const;
    // The error to show for a failed reply, preferring the API's own message.
    static QString errorMessage(QNetworkReply *reply, const QByteArray &body);
    void fetchFile(const QUrl &link, const QString &path);
    void track(QNetworkReply *reply);

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl;
    QString m_apiKey;
    QPointer<QNetworkReply> m_reply;
};
