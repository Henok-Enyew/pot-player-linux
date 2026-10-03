#pragma once

#include <QProcess>
#include <QStandardPaths>
#include <QString>

// Generates a test video at `path` with ffmpeg (10 minutes by default), with
// a keyframe every second so seeks land exactly. Returns false if ffmpeg is
// missing or fails.
inline bool makeTestClip(const QString &path, int seconds = 600)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return false;
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc=duration=%1:size=64x48:rate=2").arg(seconds),
                           QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"), QStringLiteral("2"),
                           path});
    return process.waitForFinished(60000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
