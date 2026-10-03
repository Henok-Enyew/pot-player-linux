#pragma once

#include "SubtitleSearch.h"

#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// A client for podnapisi.net's public search, which needs no account or
// key: it searches by title (plus year or season and episode) and hands
// out subtitles as ZIP archives.
class PodnapisiClient : public QObject
{
    Q_OBJECT

public:
    explicit PodnapisiClient(QObject *parent = nullptr);

    // https://www.podnapisi.net, or $POTPLAYER_PODNAPISI_URL.
    QUrl baseUrl() const { return m_baseUrl; }
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }

    // Searches by title; searchFinished() or failed() follows.
    void search(const SubtitleQuery &query);
    // Downloads and unpacks `result` to `path` (its extension follows the
    // file in the archive); downloadFinished() or failed() follows.
    void download(const SubtitleResult &result, const QString &path);
    void cancel();
    bool isBusy() const { return !m_reply.isNull(); }

    // Parses the XML search response; public for tests.
    static QList<SubtitleResult> parseResults(const QByteArray &xml);
    // podnapisi.net's code for a language ("pt-BR" -> "pt-br").
    static QString languageCode(const QString &code);

Q_SIGNALS:
    void searchFinished(const QList<SubtitleResult> &results);
    void downloadFinished(const QString &path);
    void failed(const QString &message);

private:
    QNetworkRequest request(const QUrl &url) const;
    void track(QNetworkReply *reply);

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl;
    QPointer<QNetworkReply> m_reply;
};
