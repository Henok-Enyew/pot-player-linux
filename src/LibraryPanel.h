#pragma once

#include "MediaLibrary.h"

#include <QSet>
#include <QTreeWidget>
#include <QWidget>

class QToolButton;

// Tree of the library: folders (browsable down to their media files) and
// playlists (listing their entries). Items can be dragged onto the player or
// the playlist.
class LibraryView : public QTreeWidget
{
    Q_OBJECT

public:
    explicit LibraryView(QWidget *parent = nullptr);

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    void keyPressEvent(QKeyEvent *event) override;

Q_SIGNALS:
    void removeRequested(QTreeWidgetItem *item);
};

// The "Library" page of the playlist drawer: the stored folders and
// playlists, with buttons to add to them. Playing and queueing are requests
// for PlaylistController; renaming and removing change the library directly.
class LibraryPanel : public QWidget
{
    Q_OBJECT

public:
    enum class EntryType { Folder, Playlist, File };
    Q_ENUM(EntryType)

    explicit LibraryPanel(QWidget *parent = nullptr);

    void setLibrary(MediaLibrary *library);
    MediaLibrary *library() const { return m_library; }
    LibraryView *view() const { return m_view; }

    // The tree item for `path` (a library folder or playlist, or an entry
    // under one that has been expanded), or nullptr.
    QTreeWidgetItem *findItem(EntryType type, const QString &path) const;
    // Lists the contents of a folder or playlist item.
    void expandItem(QTreeWidgetItem *item);

Q_SIGNALS:
    // Replace the queue with a folder, a playlist, or the folder of a file
    // starting at that file.
    // With `start` >= 0, playback starts at that entry of the playlist.
    void playRequested(LibraryPanel::EntryType type, const QString &path, int start = -1);
    // Add to the end of the queue.
    void queueRequested(LibraryPanel::EntryType type, const QString &path);
    void addFolderRequested();
    void addPlaylistFileRequested();
    void saveQueueRequested();
    // Overwrite a playlist file with the queue.
    void overwritePlaylistRequested(const QString &path);

private:
    void rebuild();
    void populate(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &pos);
    void renameItem(QTreeWidgetItem *item);
    void removeItem(QTreeWidgetItem *item);
    void updateButtons();

    MediaLibrary *m_library = nullptr;
    LibraryView *m_view;
    QTreeWidgetItem *m_folders = nullptr;
    QTreeWidgetItem *m_playlists = nullptr;
    QToolButton *m_removeButton = nullptr;
    // Items to expand again as the tree is rebuilt, as "type:path" keys.
    QSet<QString> m_expandedKeys;
};
