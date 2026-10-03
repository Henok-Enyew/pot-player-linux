#pragma once

#include <QByteArray>
#include <QStringList>

#include <mpv/client.h>

#include <vector>

namespace MpvDetail {

// Runs `run` with `args` as a null-terminated argv of UTF-8 strings.
template<typename Run>
int withArgv(const QStringList &args, Run run)
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
    return run(argv.data());
}

} // namespace MpvDetail

// Runs an mpv command asynchronously, e.g. {"seek", "5", "relative"}.
// `reply` comes back as the reply_userdata of its MPV_EVENT_COMMAND_REPLY.
// Returns mpv's error code: the command did not run if it is negative, e.g.
// MPV_ERROR_EVENT_QUEUE_FULL while about a thousand replies are pending.
inline int mpvCommandAsync(mpv_handle *mpv, const QStringList &args, uint64_t reply = 0)
{
    return MpvDetail::withArgv(args, [mpv, reply](const char **argv) { return mpv_command_async(mpv, reply, argv); });
}
