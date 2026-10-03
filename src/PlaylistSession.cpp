#include "PlaylistSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr int kFormatVersion = 1;

QString settingsFile()
{
    return PlaylistSession::configDir() + QStringLiteral("/settings.ini");
}

bool boolSetting(const QString &key, bool defaultValue)
{
    return QSettings(settingsFile(), QSettings::IniFormat).value(key, defaultValue).toBool();
}

void setBoolSetting(const QString &key, bool value)
{
    QSettings(settingsFile(), QSettings::IniFormat).setValue(key, value);
}

} // namespace

namespace PlaylistSession {

QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/potplayer-linux");
}

QString sessionFile()
{
    return configDir() + QStringLiteral("/last_playlist.json");
}

bool save(const State &state, const QString &path)
{
    QJsonArray entries;
    for (const PlaylistOps::Entry &entry : state.entries) {
        QJsonObject object{{QStringLiteral("filename"), entry.filename}};
        if (!entry.title.isEmpty())
            object.insert(QStringLiteral("title"), entry.title);
        if (entry.duration >= 0)
            object.insert(QStringLiteral("duration"), entry.duration);
        entries.append(object);
    }
    const QJsonObject root{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("current"), state.current},
        {QStringLiteral("position"), state.position},
        {QStringLiteral("entries"), entries},
    };

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

std::optional<State> load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    if (!document.isObject() || root.value(QStringLiteral("version")).toInt() != kFormatVersion)
        return std::nullopt;

    State state;
    for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        PlaylistOps::Entry entry;
        entry.filename = object.value(QStringLiteral("filename")).toString();
        entry.title = object.value(QStringLiteral("title")).toString();
        entry.duration = object.value(QStringLiteral("duration")).toDouble(-1);
        if (!entry.filename.isEmpty())
            state.entries.append(entry);
    }
    state.current = root.value(QStringLiteral("current")).toInt(-1);
    if (state.current >= state.entries.size())
        state.current = -1;
    state.position = std::max(0.0, root.value(QStringLiteral("position")).toDouble());
    return state;
}

bool rememberPlaylist()
{
    return boolSetting(QStringLiteral("playlist/remember"), true);
}

void setRememberPlaylist(bool enabled)
{
    setBoolSetting(QStringLiteral("playlist/remember"), enabled);
    if (!enabled)
        QFile::remove(sessionFile());
}

bool resumePlayback()
{
    return boolSetting(QStringLiteral("playlist/resume"), true);
}

void setResumePlayback(bool enabled)
{
    setBoolSetting(QStringLiteral("playlist/resume"), enabled);
}

int drawerWidth(int defaultWidth)
{
    return QSettings(settingsFile(), QSettings::IniFormat).value(QStringLiteral("playlist/width"), defaultWidth).toInt();
}

void setDrawerWidth(int width)
{
    QSettings(settingsFile(), QSettings::IniFormat).setValue(QStringLiteral("playlist/width"), width);
}

} // namespace PlaylistSession
