#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Folders and playlists kept in the player's library, saved in
// ~/.config/top-player/library.json. Playlists saved from the queue are
// written to ~/.config/top-player/playlists/ and belong to the library;
// other playlist files are only referenced.
class MediaLibrary : public QObject
{
    Q_OBJECT

public:
    enum class Kind { Folder, Playlist };
    struct Item {
        Kind kind = Kind::Folder;
        QString name;
        QString path; // absolute
    };

    explicit MediaLibrary(const QString &file = defaultFile(), QObject *parent = nullptr);

    static QString defaultFile();
    // Where playlists saved to the library are written.
    static QString playlistsDir();

    QString file() const { return m_file; }
    QList<Item> items(Kind kind) const;
    const Item *find(Kind kind, const QString &path) const;
    bool contains(Kind kind, const QString &path) const { return find(kind, path); }

    // Adds an item, named after its file or folder unless `name` is given.
    // Returns false if it is already in the library.
    bool add(Kind kind, const QString &path, const QString &name = QString());
    // Removes an item. A playlist the library owns is deleted from disk.
    bool remove(Kind kind, const QString &path);
    bool rename(Kind kind, const QString &path, const QString &name);

    // True for playlists stored in playlistsDir(), which are deleted with their entry.
    static bool ownsPlaylist(const QString &path);
    // A file name in playlistsDir() for a new playlist named `name`, not yet in use.
    static QString newPlaylistPath(const QString &name);

    // Re-reads the library file.
    void reload();

Q_SIGNALS:
    void changed();

private:
    bool save() const;

    QString m_file;
    QList<Item> m_items;
};
