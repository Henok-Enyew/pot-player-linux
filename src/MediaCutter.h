#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

// Cuts a range out of a media file with the ffmpeg command-line tool,
// asynchronously: either a lossless stream copy or the audio alone.
class MediaCutter : public QObject
{
    Q_OBJECT

public:
    enum class Mode { StreamCopy, AudioOnly };
    enum class AudioFormat { Mp3, Aac, Flac };

    struct Job {
        QString input;
        QString output;
        double start = 0; // seconds
        double end = 0;
        Mode mode = Mode::StreamCopy;
        AudioFormat audioFormat = AudioFormat::Mp3;
    };

    explicit MediaCutter(QObject *parent = nullptr);
    ~MediaCutter() override;

    // The ffmpeg found on $PATH, or empty.
    static QString ffmpegPath();
    // The arguments for `job` (without the program name).
    static QStringList arguments(const Job &job);
    // "mp3", "aac", "flac".
    static QString audioSuffix(AudioFormat format);

    // "HH:MM:SS.zzz".
    static QString formatTimestamp(double seconds);
    // Parses "HH:MM:SS.zzz", "MM:SS(.zzz)" or plain seconds; -1 if invalid.
    static double parseTimestamp(const QString &text);
    // The last "time=HH:MM:SS.xx" (or "out_time_us=N") in ffmpeg's stderr
    // output, in seconds; -1 if there is none.
    static double parseProgress(const QByteArray &output);

    // Starts `job`. Emits progress() as ffmpeg reports, then finished().
    bool start(const Job &job);
    // Stops ffmpeg and removes the partial output; finished() is not emitted.
    void cancel();
    bool isRunning() const;

Q_SIGNALS:
    // 0..1 of the range written so far.
    void progress(double fraction);
    // `error` is empty on success.
    void finished(bool ok, const QString &output, const QString &error);

private:
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QPointer<QProcess> m_process;
    Job m_job;
    QByteArray m_log; // the tail of stderr, for error messages
};
