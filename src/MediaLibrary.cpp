#include "MediaLibrary.h"
#include "PlaylistSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

constexpr int kFormatVersion = 1;

QString kindKey(MediaLibrary::Kind kind)
{
    return kind == MediaLibrary::Kind::Folder ? QStringLiteral("folders") : QStringLiteral("playlists");
}

QString absolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString defaultName(MediaLibrary::Kind kind, const QString &path)
{
    const QFileInfo info(path);
    const QString name = kind == MediaLibrary::Kind::Folder ? info.fileName() : info.completeBaseName();
    return name.isEmpty() ? path : name;
}

} // namespace

MediaLibrary::MediaLibrary(const QString &file, QObject *parent)
    : QObject(parent)
    , m_file(file)
{
    reload();
}

QString MediaLibrary::defaultFile()
{
    return PlaylistSession::configDir() + QStringLiteral("/library.json");
}

QString MediaLibrary::playlistsDir()
{
    return PlaylistSession::configDir() + QStringLiteral("/playlists");
}

QList<MediaLibrary::Item> MediaLibrary::items(Kind kind) const
{
    QList<Item> result;
    for (const Item &item : m_items) {
        if (item.kind == kind)
            result.append(item);
    }
    return result;
}

const MediaLibrary::Item *MediaLibrary::find(Kind kind, const QString &path) const
{
    const QString key = absolute(path);
    for (const Item &item : m_items) {
        if (item.kind == kind && item.path == key)
            return &item;
    }
    return nullptr;
}

bool MediaLibrary::add(Kind kind, const QString &path, const QString &name)
{
    if (path.isEmpty() || contains(kind, path))
        return false;
    const QString full = absolute(path);
    const QString trimmed = name.trimmed();
    m_items.append({kind, trimmed.isEmpty() ? defaultName(kind, full) : trimmed, full});
    save();
    Q_EMIT changed();
    return true;
}

bool MediaLibrary::remove(Kind kind, const QString &path)
{
    const Item *item = find(kind, path);
    if (!item)
        return false;
    const QString full = item->path;
    m_items.removeAt(item - m_items.constData());
    if (kind == Kind::Playlist && ownsPlaylist(full))
        QFile::remove(full);
    save();
    Q_EMIT changed();
    return true;
}

bool MediaLibrary::rename(Kind kind, const QString &path, const QString &name)
{
    const Item *item = find(kind, path);
    const QString trimmed = name.trimmed();
    if (!item || trimmed.isEmpty())
        return false;
    m_items[item - m_items.constData()].name = trimmed;
    save();
    Q_EMIT changed();
    return true;
}

bool MediaLibrary::ownsPlaylist(const QString &path)
{
    return QFileInfo(absolute(path)).absolutePath() == absolute(playlistsDir());
}

QString MediaLibrary::newPlaylistPath(const QString &name)
{
    // Characters that are awkward or invalid in file names.
    QString base = name.trimmed();
    for (QChar &c : base) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c.category() == QChar::Other_Control)
            c = QLatin1Char('_');
    }
    if (base.isEmpty() || base.startsWith(QLatin1Char('.')))
        base.prepend(QStringLiteral("Playlist"));
    const QDir dir(playlistsDir());
    QString path = dir.filePath(base + QStringLiteral(".m3u8"));
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir.filePath(QStringLiteral("%1 (%2).m3u8").arg(base).arg(n));
    return path;
}

void MediaLibrary::reload()
{
    m_items.clear();
    QFile file(m_file);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        if (root.value(QStringLiteral("version")).toInt() == kFormatVersion) {
            for (Kind kind : {Kind::Folder, Kind::Playlist}) {
                for (const QJsonValue &value : root.value(kindKey(kind)).toArray()) {
                    const QJsonObject object = value.toObject();
                    const QString path = object.value(QStringLiteral("path")).toString();
                    if (path.isEmpty() || contains(kind, path))
                        continue;
                    QString name = object.value(QStringLiteral("name")).toString();
                    if (name.isEmpty())
                        name = defaultName(kind, path);
                    m_items.append({kind, name, absolute(path)});
                }
            }
        }
    }
    Q_EMIT changed();
}

bool MediaLibrary::save() const
{
    QJsonObject root{{QStringLiteral("version"), kFormatVersion}};
    for (Kind kind : {Kind::Folder, Kind::Playlist}) {
        QJsonArray array;
        for (const Item &item : m_items) {
            if (item.kind == kind)
                array.append(QJsonObject{{QStringLiteral("name"), item.name}, {QStringLiteral("path"), item.path}});
        }
        root.insert(kindKey(kind), array);
    }
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}
