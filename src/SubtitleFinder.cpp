#include "SubtitleFinder.h"
#include "OpenSubtitlesClient.h"
#include "PodnapisiClient.h"

#include <algorithm>

namespace {

const QString kPodnapisi = QStringLiteral("Podnapisi");
const QString kOpenSubtitles = QStringLiteral("OpenSubtitles");

} // namespace

SubtitleFinder::SubtitleFinder(QObject *parent)
    : QObject(parent)
    , m_podnapisi(new PodnapisiClient(this))
    , m_openSubtitles(new OpenSubtitlesClient(this))
{
    connect(m_podnapisi, &PodnapisiClient::searchFinished, this, &SubtitleFinder::onResults);
    connect(m_podnapisi, &PodnapisiClient::failed, this, &SubtitleFinder::onFailed);
    connect(m_openSubtitles, &OpenSubtitlesClient::searchFinished, this, &SubtitleFinder::onResults);
    connect(m_openSubtitles, &OpenSubtitlesClient::failed, this, &SubtitleFinder::onFailed);
    auto downloaded = [this](const QString &path) {
        m_state = State::Idle;
        Q_EMIT downloadFinished(path);
    };
    connect(m_podnapisi, &PodnapisiClient::downloadFinished, this, downloaded);
    connect(m_openSubtitles, &OpenSubtitlesClient::downloadFinished, this, downloaded);
}

bool SubtitleFinder::hasOpenSubtitles()
{
    return !SubtitleSearch::apiKey().isEmpty();
}

bool SubtitleFinder::isBusy() const
{
    return m_state != State::Idle;
}

void SubtitleFinder::cancel()
{
    m_state = State::Idle;
    m_podnapisi->cancel();
    m_openSubtitles->cancel();
}

void SubtitleFinder::search(Mode mode, const SubtitleQuery &query, const QString &mediaPath)
{
    cancel();
    m_query = query;
    m_mediaPath = mediaPath;
    m_note.clear();
    m_errors.clear();
    m_anyAnswered = false;

    if (mode == Mode::Name || query.movieHash.isEmpty()) {
        if (mode == Mode::Hash)
            m_note = tr("This file can't be matched exactly (it is too small or not a local file), so this is a search by name.");
        searchByName();
        return;
    }
    if (!hasOpenSubtitles()) {
        m_note = tr("Exact matching needs OpenSubtitles, which isn't set up in this build; "
                    "these are matches by name, with the ones named like your file first.");
        searchByName();
        return;
    }
    // Only the hash: a title or year would also bring in other releases.
    SubtitleQuery exact;
    exact.languages = query.languages;
    exact.movieHash = query.movieHash;
    m_state = State::Exact;
    m_openSubtitles->setApiKey(SubtitleSearch::apiKey());
    m_openSubtitles->search(exact);
}

void SubtitleFinder::onResults(const QList<SubtitleResult> &results)
{
    switch (m_state) {
    case State::Exact: {
        QList<SubtitleResult> exactMatches;
        for (const SubtitleResult &result : results) {
            if (result.hashMatch)
                exactMatches.append(result);
        }
        if (!exactMatches.isEmpty()) {
            m_state = State::Idle;
            Q_EMIT searchFinished(exactMatches, QString());
            return;
        }
        m_note = tr("No exact match for this file; these are matches by name.");
        searchByName();
        return;
    }
    case State::Name:
        m_anyAnswered = true;
        if (!results.isEmpty())
            finish(results);
        else
            nextProvider();
        return;
    case State::Idle:
    case State::Download:
        return;
    }
}

void SubtitleFinder::onFailed(const QString &message)
{
    switch (m_state) {
    case State::Exact:
        m_errors.append(message);
        m_note = tr("The exact search failed (%1); these are matches by name.").arg(message);
        searchByName();
        return;
    case State::Name:
        m_errors.append(message);
        nextProvider();
        return;
    case State::Download:
        m_state = State::Idle;
        Q_EMIT failed(message);
        return;
    case State::Idle:
        return;
    }
}

void SubtitleFinder::searchByName()
{
    if (m_query.text.trimmed().isEmpty()) {
        m_state = State::Idle;
        Q_EMIT failed(tr("Enter a title to search for."));
        return;
    }
    m_state = State::Name;
    m_providers = {kPodnapisi};
    if (hasOpenSubtitles())
        m_providers.append(kOpenSubtitles);
    nextProvider();
}

void SubtitleFinder::nextProvider()
{
    if (m_providers.isEmpty()) {
        m_state = State::Idle;
        if (m_anyAnswered) {
            Q_EMIT searchFinished({}, m_note);
            return;
        }
        // Every provider failed: the first reason (offline, timeout) is the telling one.
        Q_EMIT failed(m_errors.isEmpty() ? tr("No subtitle service could be reached.") : m_errors.first());
        return;
    }
    const QString provider = m_providers.takeFirst();
    SubtitleQuery query = m_query;
    query.movieHash.clear();
    if (provider == kPodnapisi) {
        m_podnapisi->search(query);
    } else {
        m_openSubtitles->setApiKey(SubtitleSearch::apiKey());
        m_openSubtitles->search(query);
    }
}

void SubtitleFinder::finish(QList<SubtitleResult> results)
{
    m_state = State::Idle;
    m_providers.clear();
    for (SubtitleResult &result : results)
        result.releaseMatch = !result.hashMatch && !m_mediaPath.isEmpty()
            && (SubtitleSearch::isSameRelease(result.release, m_mediaPath)
                || SubtitleSearch::isSameRelease(result.fileName, m_mediaPath));
    std::stable_sort(results.begin(), results.end(), [](const SubtitleResult &a, const SubtitleResult &b) {
        auto rank = [](const SubtitleResult &r) { return r.hashMatch ? 0 : r.releaseMatch ? 1 : 2; };
        return rank(a) < rank(b);
    });
    Q_EMIT searchFinished(results, m_note);
}

void SubtitleFinder::download(const SubtitleResult &result, const QString &path)
{
    cancel();
    m_state = State::Download;
    if (result.provider == kOpenSubtitles) {
        m_openSubtitles->setApiKey(SubtitleSearch::apiKey());
        m_openSubtitles->download(result, path);
    } else {
        m_podnapisi->download(result, path);
    }
}
