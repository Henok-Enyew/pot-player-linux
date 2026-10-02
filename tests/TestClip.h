#pragma once

#include <QProcess>
#include <QStandardPaths>
#include <QString>

// Generates a 10-minute test video at `path` with ffmpeg, with a keyframe
// every second so seeks land exactly. Returns false if ffmpeg is missing or fails.
inline bool makeTestClip(const QString &path)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return false;
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc=duration=600:size=64x48:rate=2"),
                           QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"), QStringLiteral("2"),
                           path});
    return process.waitForFinished(60000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
