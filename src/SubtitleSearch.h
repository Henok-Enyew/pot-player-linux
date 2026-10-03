#pragma once

#include <QList>
#include <QString>
#include <QUrl>

class QNetworkReply;

// A subtitle found by one of the providers.
struct SubtitleResult {
    QString provider;   // "Podnapisi", "OpenSubtitles"
    QString id;         // the provider's id for downloading
    int fileId = 0;     // OpenSubtitles file id
    QString language;   // "en", "pt-BR"
    QString fileName;   // the title or release shown in the list
    QString release;
    qint64 downloads = 0;
    double rating = 0;  // 0-10, 0 if not rated
    QString format;     // "srt", "ass", ...
    bool hearingImpaired = false;
    bool hashMatch = false;    // made for this exact file (movie hash)
    bool releaseMatch = false; // its release name matches the file's
    bool machineTranslated = false;
    QString uploader;
    QUrl pageUrl;
};

// What to search for.
struct SubtitleQuery {
    QString text;          // title; may be empty with a hash
    QStringList languages; // codes ("en", "pt-BR"); empty for all
    QString movieHash;     // 16 hex digits, or empty
    int year = 0;
    int season = -1;
    int episode = -1;
};

// Helpers for finding subtitles online: what to search for, the OpenSubtitles
// movie hash, languages, where downloads go, and the download settings.
namespace SubtitleSearch {

// What a media file name says about its contents.
struct ParsedName {
    QString title;    // "The Matrix", "Breaking Bad"
    int year = 0;     // 0 if not in the name
    int season = -1;  // -1 unless the name has S01E02 or 1x02
    int episode = -1;
};

// Cleans a release file name ("The.Matrix.1999.1080p.BluRay.x264-GRP.mkv",
// "Breaking.Bad.S01E02.720p.HDTV.mkv") into a title for searching.
ParsedName parseFileName(const QString &path);

// True if `release` names the same release as the media file at `mediaPath`
// ("The.Matrix.1999.1080p.BluRay.x264-GRP"), so it is likely in sync.
bool isSameRelease(const QString &release, const QString &mediaPath);

// The User-Agent sent to subtitle services.
QByteArray userAgent();
// A message for a failed request to `service` that a user can act on
// ("Can't reach podnapisi.net. Check your internet connection.").
QString networkErrorMessage(const QString &service, QNetworkReply *reply);

struct Language {
    QString code; // as OpenSubtitles writes it: "en", "pt-BR", "zh-CN"
    QString name; // "English", "Portuguese (Brazil)"
};
const QList<Language> &languages();
// The display name for `code`, or `code` itself if unknown.
QString languageName(const QString &code);
// The system locale's language if OpenSubtitles has it, else English.
QString systemLanguage();

// Where a downloaded subtitle for `mediaPath` goes: beside the video as
// "<video name>.<language>.<format>" when `besideVideo` and that folder is
// writable, else in ~/.cache/potplayer-linux/subtitles. Never an existing file.
QString savePath(const QString &mediaPath, const QString &language, const QString &format, bool besideVideo);
QString cacheDir();

// Settings, in ~/.config/potplayer-linux/settings.ini. The API key is
// optional: OpenSubtitles (exact hash matching) is used when there is one.
QString userApiKey();
void setUserApiKey(const QString &key);
// The user's key, else the one built in (empty if neither).
QString apiKey();
QString builtInApiKey();
QString language(); // last language searched, else systemLanguage()
void setLanguage(const QString &code);
bool saveBesideVideo(); // default false: ~/.cache/potplayer-linux/subtitles
void setSaveBesideVideo(bool besideVideo);

} // namespace SubtitleSearch
