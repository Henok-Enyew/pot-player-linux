#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

// Downloads videos and audio from YouTube, TikTok, Instagram and the other
// sites yt-dlp supports, by running the yt-dlp command-line tool.
class MediaDownloader : public QObject
{
    Q_OBJECT

public:
    enum class Format { Best, Max2160, Max1080, Max720, AudioMp3 };

    struct Progress {
        double percent = -1; // 0..100
        QString total;       // "10.03MiB"
        QString speed;       // "2.04MiB/s"
        QString eta;         // "00:03"
    };

    explicit MediaDownloader(QObject *parent = nullptr);
    ~MediaDownloader() override;

    // The yt-dlp found on $PATH, or empty.
    static QString executable();
    static QStringList arguments(const QString &url, Format format, const QString &directory);
    // mpv's ytdl-format for streaming in `format`.
    static QString streamFormat(Format format);

    // Cleans pasted text into a URL ("youtu.be/x" -> "https://youtu.be/x");
    // empty if it isn't one.
    static QString normalizeUrl(const QString &text);
    // "YouTube", "TikTok", ... for the social and video sites the dialog knows,
    // else empty (yt-dlp may still support the URL).
    static QString platformName(const QString &url);
    static bool isKnownSite(const QString &url) { return !platformName(url).isEmpty(); }
    // Parses a "[download]  45.3% of 10.00MiB at 1.2MiB/s ETA 00:05" line.
    static std::optional<Progress> parseProgress(const QString &line);

    bool start(const QString &url, Format format, const QString &directory);
    // Stops yt-dlp; finished() is not emitted. Partial ".part" files stay
    // for yt-dlp to resume next time.
    void cancel();
    bool isRunning() const;

Q_SIGNALS:
    void progress(const MediaDownloader::Progress &progress);
    // What yt-dlp is doing: "Downloading", "Merging formats", "Extracting audio".
    void stageChanged(const QString &stage);
    // `path` is the downloaded file; `error` is empty on success.
    void finished(bool ok, const QString &path, const QString &error);

private:
    void readLines(QByteArray &buffer, const QByteArray &data);
    void handleLine(const QString &line);
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QPointer<QProcess> m_process;
    QByteArray m_stdout;
    QByteArray m_stderr;
    QString m_path;
    QStringList m_errors;
};
