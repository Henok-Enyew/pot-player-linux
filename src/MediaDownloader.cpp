#include "MediaDownloader.h"

#include <QDir>
#include <algorithm>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

struct Site {
    const char *domain; // matches the host and its subdomains
    const char *name;
};

const Site kSites[] = {
    {"youtube.com", "YouTube"},
    {"youtu.be", "YouTube"},
    {"youtube-nocookie.com", "YouTube"},
    {"tiktok.com", "TikTok"},
    {"instagram.com", "Instagram"},
    {"twitter.com", "X (Twitter)"},
    {"x.com", "X (Twitter)"},
    {"facebook.com", "Facebook"},
    {"fb.watch", "Facebook"},
    {"vimeo.com", "Vimeo"},
    {"twitch.tv", "Twitch"},
    {"reddit.com", "Reddit"},
    {"redd.it", "Reddit"},
    {"dailymotion.com", "Dailymotion"},
    {"dai.ly", "Dailymotion"},
    {"soundcloud.com", "SoundCloud"},
    {"bilibili.com", "Bilibili"},
    {"pinterest.com", "Pinterest"},
    {"threads.net", "Threads"},
    {"bsky.app", "Bluesky"},
};

QString formatSelector(MediaDownloader::Format format)
{
    switch (format) {
    case MediaDownloader::Format::Best:
        return QStringLiteral("bv*+ba/b");
    case MediaDownloader::Format::Max2160:
        return QStringLiteral("bv*[height<=2160]+ba/b[height<=2160]/b");
    case MediaDownloader::Format::Max1080:
        return QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]/b");
    case MediaDownloader::Format::Max720:
        return QStringLiteral("bv*[height<=720]+ba/b[height<=720]/b");
    case MediaDownloader::Format::AudioMp3:
        return QStringLiteral("ba/b");
    }
    return QStringLiteral("bv*+ba/b");
}

} // namespace

MediaDownloader::MediaDownloader(QObject *parent)
    : QObject(parent)
{
}

MediaDownloader::~MediaDownloader()
{
    cancel();
}

QString MediaDownloader::executable()
{
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

QStringList MediaDownloader::arguments(const QString &url, Format format, const QString &directory)
{
    QStringList args{
        // One progress line per update, even though --print makes yt-dlp quiet.
        QStringLiteral("--newline"), QStringLiteral("--progress"),
        QStringLiteral("--no-playlist"), QStringLiteral("--no-mtime"),
        QStringLiteral("-P"), directory,
        QStringLiteral("-o"), QStringLiteral("%(title).150B [%(id)s].%(ext)s"),
        // The final file, after merging or audio extraction.
        QStringLiteral("--print"), QStringLiteral("after_move:filepath"),
    };
    if (format == Format::AudioMp3) {
        args << QStringLiteral("-f") << formatSelector(format) << QStringLiteral("-x")
             << QStringLiteral("--audio-format") << QStringLiteral("mp3")
             << QStringLiteral("--audio-quality") << QStringLiteral("0");
    } else {
        args << QStringLiteral("-f") << formatSelector(format)
             << QStringLiteral("--merge-output-format") << QStringLiteral("mp4");
    }
    // "--" so that a URL can never be read as an option.
    args << QStringLiteral("--") << url;
    return args;
}

QString MediaDownloader::streamFormat(Format format)
{
    return formatSelector(format);
}

QString MediaDownloader::normalizeUrl(const QString &text)
{
    QString candidate = text.trimmed();
    if (candidate.isEmpty() || candidate.contains(QLatin1Char('\n')) || candidate.contains(QLatin1Char(' ')))
        return {};
    if (!candidate.contains(QLatin1String("://")))
        candidate.prepend(QStringLiteral("https://"));
    const QUrl url(candidate, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || !url.host().contains(QLatin1Char('.'))
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        return {};
    }
    return url.toString();
}

QString MediaDownloader::platformName(const QString &text)
{
    const QString normalized = normalizeUrl(text);
    if (normalized.isEmpty())
        return {};
    const QString host = QUrl(normalized).host().toLower();
    for (const Site &site : kSites) {
        const QString domain = QString::fromLatin1(site.domain);
        if (host == domain || host.endsWith(QLatin1Char('.') + domain))
            return QString::fromLatin1(site.name);
    }
    return {};
}

std::optional<MediaDownloader::Progress> MediaDownloader::parseProgress(const QString &line)
{
    static const QRegularExpression percent(QStringLiteral("^\\[download\\]\\s+([\\d.]+)%"));
    const QRegularExpressionMatch match = percent.match(line.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    Progress progress;
    progress.percent = std::clamp(match.captured(1).toDouble(), 0.0, 100.0);
    static const QRegularExpression total(QStringLiteral("\\bof\\s+~?\\s*([\\d.]+\\s*[KMGT]?i?B)\\b"));
    static const QRegularExpression speed(QStringLiteral("\\bat\\s+([\\d.]+\\s*[KMGT]?i?B/s)"));
    static const QRegularExpression eta(QStringLiteral("\\bETA\\s+([\\d:]+)"));
    if (const auto m = total.match(line); m.hasMatch())
        progress.total = m.captured(1).remove(QLatin1Char(' '));
    if (const auto m = speed.match(line); m.hasMatch())
        progress.speed = m.captured(1).remove(QLatin1Char(' '));
    if (const auto m = eta.match(line); m.hasMatch())
        progress.eta = m.captured(1);
    return progress;
}

bool MediaDownloader::start(const QString &url, Format format, const QString &directory)
{
    const QString program = executable();
    if (isRunning() || program.isEmpty())
        return false;
    QDir().mkpath(directory);
    m_stdout.clear();
    m_stderr.clear();
    m_path.clear();
    m_errors.clear();

    auto *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process] { readLines(m_stdout, process->readAllStandardOutput()); });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process] { readLines(m_stderr, process->readAllStandardError()); });
    connect(process, &QProcess::finished, this, &MediaDownloader::onFinished);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process)
            return;
        m_process = nullptr;
        process->deleteLater();
        Q_EMIT finished(false, {}, tr("Could not start yt-dlp: %1").arg(process->errorString()));
    });
    Q_EMIT stageChanged(tr("Starting..."));
    process->start(program, arguments(url, format, directory));
    return true;
}

void MediaDownloader::readLines(QByteArray &buffer, const QByteArray &data)
{
    buffer += data;
    qsizetype end;
    while ((end = buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(buffer.left(end)).trimmed();
        buffer.remove(0, end + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

void MediaDownloader::handleLine(const QString &line)
{
    if (const auto parsed = parseProgress(line)) {
        Q_EMIT progress(*parsed);
        return;
    }
    if (line.startsWith(QLatin1String("ERROR:"))) {
        m_errors.append(line.mid(6).trimmed());
    } else if (line.startsWith(QLatin1String("[Merger]"))) {
        Q_EMIT stageChanged(tr("Merging video and audio..."));
    } else if (line.startsWith(QLatin1String("[ExtractAudio]"))) {
        Q_EMIT stageChanged(tr("Converting to MP3..."));
    } else if (line.startsWith(QLatin1String("[download] Destination:"))) {
        Q_EMIT stageChanged(tr("Downloading %1").arg(QFileInfo(line.section(QLatin1Char(':'), 1).trimmed()).fileName()));
    } else if (!line.startsWith(QLatin1Char('[')) && QFileInfo(line).isAbsolute()) {
        // --print after_move:filepath
        m_path = line;
    }
}

void MediaDownloader::onFinished(int exitCode, QProcess::ExitStatus status)
{
    QProcess *process = m_process.data();
    if (!process)
        return;
    m_process = nullptr;
    process->deleteLater();
    // Lines without a trailing newline.
    readLines(m_stdout, "\n");
    readLines(m_stderr, "\n");
    if (status == QProcess::NormalExit && exitCode == 0 && !m_path.isEmpty() && QFileInfo::exists(m_path)) {
        Q_EMIT finished(true, m_path, {});
        return;
    }
    QString error = m_errors.join(QLatin1Char('\n'));
    if (error.isEmpty())
        error = status == QProcess::CrashExit ? tr("yt-dlp crashed.") : tr("yt-dlp failed (exit code %1).").arg(exitCode);
    Q_EMIT finished(false, {}, error);
}

void MediaDownloader::cancel()
{
    QProcess *process = m_process.data();
    if (!process)
        return;
    m_process = nullptr;
    process->disconnect(this);
    process->terminate();
    if (!process->waitForFinished(3000))
        process->kill();
    process->deleteLater();
}

bool MediaDownloader::isRunning() const
{
    return !m_process.isNull();
}
