#pragma once

#include "SubtitleSearch.h"

#include <QObject>

class OpenSubtitlesClient;
class PodnapisiClient;

// Searches the subtitle providers in turn and downloads from whichever one
// a result came from:
//  - by hash: OpenSubtitles' exact (movie hash) matches, when an API key is
//    available; otherwise, or when there are none, a search by name;
//  - by name: podnapisi.net (no key needed), then OpenSubtitles.
// Results whose release name matches the file are listed first.
class SubtitleFinder : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Hash, Name };

    explicit SubtitleFinder(QObject *parent = nullptr);

    PodnapisiClient *podnapisi() const { return m_podnapisi; }
    OpenSubtitlesClient *openSubtitles() const { return m_openSubtitles; }
    // OpenSubtitles takes part only with an API key (built in or the user's).
    static bool hasOpenSubtitles();

    // `mediaPath` is the local file, for release matching; may be empty.
    void search(Mode mode, const SubtitleQuery &query, const QString &mediaPath);
    void download(const SubtitleResult &result, const QString &path);
    void cancel();
    bool isBusy() const;

Q_SIGNALS:
    // `note` explains a fallback ("No exact match; searched by name.").
    void searchFinished(const QList<SubtitleResult> &results, const QString &note);
    void downloadFinished(const QString &path);
    void failed(const QString &message);

private:
    enum class State { Idle, Exact, Name, Download };

    void onResults(const QList<SubtitleResult> &results);
    void onFailed(const QString &message);
    void searchByName();
    void nextProvider();
    void finish(QList<SubtitleResult> results);

    PodnapisiClient *m_podnapisi;
    OpenSubtitlesClient *m_openSubtitles;
    SubtitleQuery m_query;
    QString m_mediaPath;
    QString m_note;
    QStringList m_providers; // still to ask, in order
    QStringList m_errors;
    bool m_anyAnswered = false;
    State m_state = State::Idle;
};
