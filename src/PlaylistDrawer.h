#pragma once

#include <QFrame>
#include <QListWidget>
#include <QVariant>

class QLabel;
class QPropertyAnimation;

// List view that turns drag-and-drop into playlist requests instead of moving
// items itself; the list is rebuilt from mpv's playlist afterwards.
class PlaylistView : public QListWidget
{
    Q_OBJECT

public:
    explicit PlaylistView(QWidget *parent = nullptr);

Q_SIGNALS:
    // Move entry `from` so it takes the place of entry `to` (mpv playlist-move semantics).
    void moveRequested(int from, int to);
    // Media files dropped at playlist index `row` (-1 = end).
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

// Collapsible right-hand playlist panel mirroring mpv's "playlist" property.
class PlaylistDrawer : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(int drawerWidth READ drawerWidth WRITE setDrawerWidth)

public:
    explicit PlaylistDrawer(QWidget *parent = nullptr);

    void setEntries(const QVariantList &playlist);

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
    void clearRequested();
    void expandedChanged(bool expanded);

private:
    PlaylistView *m_view;
    QLabel *m_countLabel;
    QPropertyAnimation *m_animation;
    bool m_expanded = false;
};
