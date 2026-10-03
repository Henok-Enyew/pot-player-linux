#pragma once

#include "PlaylistOps.h"

#include <QFrame>
#include <QListWidget>
#include <QVariant>

class QLabel;
class QLineEdit;
class QMenu;
class QPropertyAnimation;

// List view that turns drag-and-drop into playlist requests instead of moving
// items itself; the list is rebuilt from mpv's playlist afterwards.
class PlaylistView : public QListWidget
{
    Q_OBJECT

public:
    explicit PlaylistView(QWidget *parent = nullptr);

    // Selected rows that the filter shows, ascending.
    QList<int> selectedVisibleRows() const;

Q_SIGNALS:
    // Move entry `from` so it takes the place of entry `to` (mpv playlist-move semantics).
    void moveRequested(int from, int to);
    // Files, folders and URLs dropped at playlist index `row` (-1 = end).
    // Folders are not expanded yet.
    void filesDropped(const QStringList &files, int row);
    void removeRequested(const QList<int> &rows);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    int dropRow(QDropEvent *event) const;
};

// Collapsible right-hand playlist panel mirroring mpv's "playlist" property,
// with a search filter and the playlist management actions.
class PlaylistDrawer : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(int drawerWidth READ drawerWidth WRITE setDrawerWidth)

public:
    explicit PlaylistDrawer(QWidget *parent = nullptr);

    // `durations` (seconds, negative if unknown) parallels `playlist` and may be empty.
    void setEntries(const QVariantList &playlist, const QList<double> &durations = {});
    void setDurations(const QList<double> &durations);

    // Hides the entries that don't match every word of `text` (in name or path).
    // The playlist itself is not changed.
    void setFilterText(const QString &text);
    QString filterText() const;

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded, bool animate = true);

    int drawerWidth() const { return width(); }
    void setDrawerWidth(int width);

Q_SIGNALS:
    void playRequested(int index);
    void moveRequested(int from, int to);
    void filesDropped(const QStringList &files, int row);
    void removeRequested(const QList<int> &rows);
    void addRequested();
    void addFolderRequested();
    void clearRequested();
    void sortRequested(PlaylistOps::SortKey key, bool ascending);
    void reverseRequested();
    void shuffleRequested();
    void removeMissingRequested();
    void removeDuplicatesRequested();
    void openPlaylistRequested();
    void savePlaylistRequested();
    void expandedChanged(bool expanded);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildMenus();
    // Checks the session options to match the saved settings.
    void syncOptions();
    void showContextMenu(const QPoint &pos);
    void applyFilter();
    void updateCount();

    PlaylistView *m_view;
    QLineEdit *m_filter;
    QLabel *m_countLabel;
    QMenu *m_sortMenu = nullptr;
    QMenu *m_moreMenu = nullptr;
    QAction *m_rememberAction = nullptr;
    QAction *m_resumeAction = nullptr;
    QPropertyAnimation *m_animation;
    bool m_expanded = false;
};
