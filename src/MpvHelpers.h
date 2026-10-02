#pragma once

#include <QByteArray>
#include <QStringList>

#include <mpv/client.h>

#include <vector>

// Runs an mpv command asynchronously, e.g. {"seek", "5", "relative"}.
inline void mpvCommandAsync(mpv_handle *mpv, const QStringList &args)
{
    std::vector<QByteArray> storage;
    storage.reserve(args.size());
    std::vector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (const QString &arg : args) {
        storage.push_back(arg.toUtf8());
        argv.push_back(storage.back().constData());
    }
    argv.push_back(nullptr);
    mpv_command_async(mpv, 0, argv.data());
}
