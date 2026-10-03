#pragma once

#include <QList>
#include <QString>

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

// The OpenSubtitles movie hash: the file size plus the 64-bit little-endian
// words of the first and last 64 KiB, as 16 hex digits. Empty for files that
// can't be read or are smaller than 64 KiB.
QString movieHash(const QString &path);

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

// Settings, in ~/.config/potplayer-linux/settings.ini.
QString userApiKey();
void setUserApiKey(const QString &key);
// The user's key, else the one built in (empty if neither).
QString apiKey();
QString builtInApiKey();
QString language(); // last language searched, else systemLanguage()
void setLanguage(const QString &code);
bool saveBesideVideo(); // default true
void setSaveBesideVideo(bool besideVideo);

} // namespace SubtitleSearch
