#include "SubtitleSearch.h"
#include "PlaylistSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QtEndian>

#ifndef TOPPLAYER_OPENSUBTITLES_API_KEY
#define TOPPLAYER_OPENSUBTITLES_API_KEY ""
#endif

namespace {

constexpr qint64 kHashChunk = 64 * 1024;

// Words that start the technical part of a release name.
const QStringList kReleaseTags{
    QStringLiteral("2160p"), QStringLiteral("1080p"), QStringLiteral("1080i"), QStringLiteral("720p"),
    QStringLiteral("576p"), QStringLiteral("480p"), QStringLiteral("4k"), QStringLiteral("uhd"),
    QStringLiteral("bluray"), QStringLiteral("blu-ray"), QStringLiteral("bdrip"), QStringLiteral("brrip"),
    QStringLiteral("bdremux"), QStringLiteral("remux"), QStringLiteral("web-dl"), QStringLiteral("webdl"),
    QStringLiteral("webrip"), QStringLiteral("web"), QStringLiteral("hdtv"), QStringLiteral("hdrip"),
    QStringLiteral("dvdrip"), QStringLiteral("dvdscr"), QStringLiteral("dvd"), QStringLiteral("hdcam"),
    QStringLiteral("cam"), QStringLiteral("x264"), QStringLiteral("x265"), QStringLiteral("h264"),
    QStringLiteral("h265"), QStringLiteral("hevc"), QStringLiteral("avc"), QStringLiteral("xvid"),
    QStringLiteral("divx"), QStringLiteral("10bit"), QStringLiteral("8bit"), QStringLiteral("hdr"),
    QStringLiteral("hdr10"), QStringLiteral("dv"), QStringLiteral("aac"), QStringLiteral("ac3"),
    QStringLiteral("dts"), QStringLiteral("ddp5"), QStringLiteral("dd5"), QStringLiteral("atmos"),
    QStringLiteral("truehd"), QStringLiteral("proper"), QStringLiteral("repack"), QStringLiteral("extended"),
    QStringLiteral("unrated"), QStringLiteral("remastered"), QStringLiteral("internal"), QStringLiteral("limited"),
    QStringLiteral("multi"), QStringLiteral("dubbed"), QStringLiteral("subbed"), QStringLiteral("amzn"),
    QStringLiteral("nf"), QStringLiteral("dsnp"), QStringLiteral("hmax"), QStringLiteral("atvp"),
};

QString settingsFile()
{
    return PlaylistSession::configDir() + QStringLiteral("/settings.ini");
}

QString setting(const QString &key, const QString &defaultValue = {})
{
    return QSettings(settingsFile(), QSettings::IniFormat).value(key, defaultValue).toString();
}

void setSetting(const QString &key, const QVariant &value)
{
    QSettings(settingsFile(), QSettings::IniFormat).setValue(key, value);
}

QString normalizedSpaces(QString text)
{
    return text.simplified();
}

} // namespace

namespace SubtitleSearch {

ParsedName parseFileName(const QString &path)
{
    ParsedName parsed;
    QString name = QFileInfo(path).completeBaseName();
    if (name.isEmpty())
        name = QFileInfo(path).fileName();
    // Release group tags in brackets ("[YTS.MX]") say nothing about the title;
    // a year in brackets does.
    static const QRegularExpression brackets(QStringLiteral("\\[[^\\]]*\\]|\\{[^}]*\\}"));
    name.remove(brackets);
    name.replace(QLatin1Char('.'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(QLatin1Char('('), QLatin1Char(' ')).replace(QLatin1Char(')'), QLatin1Char(' '));
    name = normalizedSpaces(name);

    qsizetype cut = name.size();
    static const QRegularExpression episode(
        QStringLiteral("\\b(?:[Ss](\\d{1,2})[ -]?[Ee](\\d{1,3})|(\\d{1,2})[xX](\\d{2,3}))\\b"));
    if (const QRegularExpressionMatch match = episode.match(name); match.hasMatch()) {
        parsed.season = (match.captured(1).isEmpty() ? match.captured(3) : match.captured(1)).toInt();
        parsed.episode = (match.captured(2).isEmpty() ? match.captured(4) : match.captured(2)).toInt();
        cut = std::min(cut, match.capturedStart());
    }
    // A year after the title ("2001 A Space Odyssey 1968" keeps its first number).
    static const QRegularExpression year(QStringLiteral("\\b(19[0-9]{2}|20[0-9]{2})\\b"));
    for (QRegularExpressionMatchIterator it = year.globalMatch(name); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedStart() == 0 || match.capturedStart() > cut)
            continue;
        if (parsed.season < 0)
            parsed.year = match.captured(1).toInt();
        cut = std::min(cut, match.capturedStart());
        break;
    }
    // Or the first technical word ("1080p", "BluRay", ...).
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    qsizetype position = 0;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const qsizetype start = name.indexOf(words[i], position);
        position = start + words[i].size();
        if (i == 0 || start >= cut)
            continue;
        const QString word = words[i].toLower().section(QLatin1Char('-'), 0, 0);
        if (kReleaseTags.contains(word) || kReleaseTags.contains(words[i].toLower())) {
            cut = start;
            break;
        }
    }
    QString title = name.left(cut);
    // Leftover separators and a trailing release group ("- GRP").
    static const QRegularExpression trailing(QStringLiteral("[\\s\\-–]+$"));
    title.remove(trailing);
    parsed.title = normalizedSpaces(title);
    if (parsed.title.isEmpty())
        parsed.title = normalizedSpaces(name);
    return parsed;
}

QString movieHash(const QString &path)
{
    QFile file(path);
    const qint64 size = file.size();
    if (size < kHashChunk || !file.open(QIODevice::ReadOnly))
        return {};
    quint64 hash = static_cast<quint64>(size);
    auto addChunk = [&](qint64 offset) {
        if (!file.seek(offset))
            return false;
        const QByteArray data = file.read(kHashChunk);
        if (data.size() != kHashChunk)
            return false;
        for (qsizetype i = 0; i < data.size(); i += 8)
            hash += qFromLittleEndian<quint64>(data.constData() + i);
        return true;
    };
    if (!addChunk(0) || !addChunk(size - kHashChunk))
        return {};
    return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

const QList<Language> &languages()
{
    static const QList<Language> list{
        {QStringLiteral("ar"), QStringLiteral("Arabic")},
        {QStringLiteral("bg"), QStringLiteral("Bulgarian")},
        {QStringLiteral("ca"), QStringLiteral("Catalan")},
        {QStringLiteral("zh-CN"), QStringLiteral("Chinese (Simplified)")},
        {QStringLiteral("zh-TW"), QStringLiteral("Chinese (Traditional)")},
        {QStringLiteral("hr"), QStringLiteral("Croatian")},
        {QStringLiteral("cs"), QStringLiteral("Czech")},
        {QStringLiteral("da"), QStringLiteral("Danish")},
        {QStringLiteral("nl"), QStringLiteral("Dutch")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("et"), QStringLiteral("Estonian")},
        {QStringLiteral("fa"), QStringLiteral("Persian")},
        {QStringLiteral("fi"), QStringLiteral("Finnish")},
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("el"), QStringLiteral("Greek")},
        {QStringLiteral("he"), QStringLiteral("Hebrew")},
        {QStringLiteral("hi"), QStringLiteral("Hindi")},
        {QStringLiteral("hu"), QStringLiteral("Hungarian")},
        {QStringLiteral("id"), QStringLiteral("Indonesian")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("ja"), QStringLiteral("Japanese")},
        {QStringLiteral("ko"), QStringLiteral("Korean")},
        {QStringLiteral("ms"), QStringLiteral("Malay")},
        {QStringLiteral("no"), QStringLiteral("Norwegian")},
        {QStringLiteral("pl"), QStringLiteral("Polish")},
        {QStringLiteral("pt-PT"), QStringLiteral("Portuguese")},
        {QStringLiteral("pt-BR"), QStringLiteral("Portuguese (Brazil)")},
        {QStringLiteral("ro"), QStringLiteral("Romanian")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("sr"), QStringLiteral("Serbian")},
        {QStringLiteral("sk"), QStringLiteral("Slovak")},
        {QStringLiteral("sl"), QStringLiteral("Slovenian")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("sv"), QStringLiteral("Swedish")},
        {QStringLiteral("th"), QStringLiteral("Thai")},
        {QStringLiteral("tr"), QStringLiteral("Turkish")},
        {QStringLiteral("uk"), QStringLiteral("Ukrainian")},
        {QStringLiteral("vi"), QStringLiteral("Vietnamese")},
    };
    return list;
}

QString languageName(const QString &code)
{
    for (const Language &language : languages()) {
        if (language.code.compare(code, Qt::CaseInsensitive) == 0)
            return language.name;
    }
    return code;
}

QString systemLanguage()
{
    const QLocale locale = QLocale::system();
    // OpenSubtitles tells some languages apart by region.
    const QString region = locale.name().replace(QLatin1Char('_'), QLatin1Char('-')); // "pt-BR"
    const QString base = region.section(QLatin1Char('-'), 0, 0);
    QString code;
    if (base == QLatin1String("pt"))
        code = locale.territory() == QLocale::Brazil ? QStringLiteral("pt-BR") : QStringLiteral("pt-PT");
    else if (base == QLatin1String("zh"))
        code = locale.script() == QLocale::TraditionalChineseScript || locale.territory() == QLocale::Taiwan
            ? QStringLiteral("zh-TW") : QStringLiteral("zh-CN");
    else if (base == QLatin1String("nb") || base == QLatin1String("nn"))
        code = QStringLiteral("no");
    else
        code = base;
    for (const Language &language : languages()) {
        if (language.code == code)
            return code;
    }
    return QStringLiteral("en");
}

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/top-player/subtitles");
}

QString savePath(const QString &mediaPath, const QString &language, const QString &format, bool besideVideo)
{
    const QFileInfo media(mediaPath);
    QString base = media.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("subtitle");
    QString dir = media.absolutePath();
    if (!besideVideo || !media.isFile() || !QFileInfo(dir).isWritable()) {
        dir = cacheDir();
        QDir().mkpath(dir);
    }
    const QString suffix = format.isEmpty() ? QStringLiteral("srt") : format.toLower();
    const QString stem = QDir(dir).filePath(base + QLatin1Char('.') + language);
    QString path = stem + QLatin1Char('.') + suffix;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QStringLiteral("%1.%2.%3").arg(stem).arg(n).arg(suffix);
    return path;
}

QString userApiKey()
{
    return setting(QStringLiteral("subtitles/apiKey")).trimmed();
}

void setUserApiKey(const QString &key)
{
    setSetting(QStringLiteral("subtitles/apiKey"), key.trimmed());
}

QString builtInApiKey()
{
    return QString::fromLatin1(TOPPLAYER_OPENSUBTITLES_API_KEY);
}

QString apiKey()
{
    const QString user = userApiKey();
    return user.isEmpty() ? builtInApiKey() : user;
}

QString language()
{
    return setting(QStringLiteral("subtitles/language"), systemLanguage());
}

void setLanguage(const QString &code)
{
    setSetting(QStringLiteral("subtitles/language"), code);
}

bool saveBesideVideo()
{
    return QSettings(settingsFile(), QSettings::IniFormat).value(QStringLiteral("subtitles/saveBesideVideo"), true).toBool();
}

void setSaveBesideVideo(bool besideVideo)
{
    setSetting(QStringLiteral("subtitles/saveBesideVideo"), besideVideo);
}

} // namespace SubtitleSearch
