#include "LibraryPanel.h"
#include "Icons.h"
#include "MediaFiles.h"
#include "PlaylistOps.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Item data. Section headers and placeholders have no type.
constexpr int kTypeRole = Qt::UserRole;
constexpr int kPathRole = Qt::UserRole + 1;
// Set once a folder's or playlist's children have been listed.
constexpr int kLoadedRole = Qt::UserRole + 2;
// Set on the folders and playlists stored in the library (not their contents).
constexpr int kStoredRole = Qt::UserRole + 3;

const QColor kMutedColor(0x7A, 0x7A, 0x7A);
const QColor kMissingColor(0x8A, 0x5A, 0x5A);

using EntryType = LibraryPanel::EntryType;

bool hasType(const QTreeWidgetItem *item)
{
    return item && item->data(0, kTypeRole).isValid();
}

EntryType typeOf(const QTreeWidgetItem *item)
{
    return static_cast<EntryType>(item->data(0, kTypeRole).toInt());
}

QString pathOf(const QTreeWidgetItem *item)
{
    return item->data(0, kPathRole).toString();
}

QString itemKey(const QTreeWidgetItem *item)
{
    return QString::number(static_cast<int>(typeOf(item))) + QLatin1Char(':') + pathOf(item);
}

MediaLibrary::Kind kindOf(EntryType type)
{
    return type == EntryType::Folder ? MediaLibrary::Kind::Folder : MediaLibrary::Kind::Playlist;
}

QTreeWidgetItem *addPlaceholder(QTreeWidgetItem *parent, const QString &text)
{
    auto *item = new QTreeWidgetItem(parent, {text});
    item->setFlags(Qt::ItemIsEnabled);
    item->setForeground(0, kMutedColor);
    QFont font = item->font(0);
    font.setItalic(true);
    item->setFont(0, font);
    return item;
}

QTreeWidgetItem *addEntry(QTreeWidgetItem *parent, EntryType type, const QString &path, const QString &name)
{
    auto *item = new QTreeWidgetItem(parent, {name});
    item->setData(0, kTypeRole, static_cast<int>(type));
    item->setData(0, kPathRole, path);
    item->setToolTip(0, path);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    switch (type) {
    case EntryType::Folder:
        item->setIcon(0, skinIcon(IconType::Folder));
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        break;
    case EntryType::Playlist:
        item->setIcon(0, skinIcon(IconType::Playlist));
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        break;
    case EntryType::File:
        break;
    }
    const QString local = MediaFiles::localPath(path);
    if (!local.isEmpty() && !QFileInfo::exists(local)) {
        item->setForeground(0, kMissingColor);
        item->setToolTip(0, QObject::tr("%1 (not found)").arg(path));
    }
    return item;
}

QFileInfoList naturallySorted(QFileInfoList list)
{
    std::sort(list.begin(), list.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return MediaFiles::naturalLess(a.fileName(), b.fileName());
    });
    return list;
}

} // namespace

LibraryView::LibraryView(QWidget *parent)
    : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("LibraryView"));
    setHeaderHidden(true);
    setColumnCount(1);
    setRootIsDecorated(true);
    setIndentation(14);
    setUniformRowHeights(true);
    setTextElideMode(Qt::ElideMiddle);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDragEnabled(true);
    // Double-click plays; the arrows (or Left/Right) open folders and playlists.
    setExpandsOnDoubleClick(false);
    setIconSize(QSize(16, 16));
    header()->setStretchLastSection(true);
}

QMimeData *LibraryView::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    QList<QUrl> urls;
    for (const QTreeWidgetItem *item : items) {
        if (!hasType(item))
            continue;
        const QString path = pathOf(item);
        const QString local = MediaFiles::localPath(path);
        urls.append(local.isEmpty() ? QUrl(path) : QUrl::fromLocalFile(local));
    }
    if (urls.isEmpty())
        return nullptr;
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

void LibraryView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        if (currentItem())
            Q_EMIT removeRequested(currentItem());
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (currentItem())
            Q_EMIT itemActivated(currentItem(), 0);
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

LibraryPanel::LibraryPanel(QWidget *parent)
    : QWidget(parent)
    , m_view(new LibraryView(this))
{
    setObjectName(QStringLiteral("LibraryPanel"));

    auto makeButton = [this](IconType icon, const QString &toolTip, const char *name) {
        auto *button = new QToolButton(this);
        button->setObjectName(QString::fromLatin1(name));
        button->setIcon(skinIcon(icon));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    QToolButton *addFolder = makeButton(IconType::Folder, tr("Add Folder to Library..."), "LibraryAddFolderButton");
    QToolButton *addPlaylist = makeButton(IconType::Open, tr("Add Playlist File to Library..."), "LibraryAddPlaylistButton");
    QToolButton *saveQueue = makeButton(IconType::Add, tr("Save Current Playlist to Library..."), "LibrarySaveButton");
    m_removeButton = makeButton(IconType::Remove, tr("Remove from Library (Del)"), "LibraryRemoveButton");

    auto *tools = new QHBoxLayout;
    tools->setContentsMargins(8, 2, 6, 6);
    tools->setSpacing(2);
    for (QToolButton *button : {addFolder, addPlaylist, saveQueue})
        tools->addWidget(button);
    tools->addStretch();
    tools->addWidget(m_removeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(tools);
    layout->addWidget(m_view, 1);

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &LibraryPanel::showContextMenu);
    connect(m_view, &QTreeWidget::itemExpanded, this, &LibraryPanel::populate);
    connect(m_view, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (hasType(item))
            m_expandedKeys.insert(itemKey(item));
    });
    connect(m_view, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (hasType(item))
            m_expandedKeys.remove(itemKey(item));
    });
    connect(m_view, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (!hasType(item))
            return;
        // An entry of a playlist plays the playlist from that entry.
        QTreeWidgetItem *parent = item->parent();
        if (typeOf(item) == EntryType::File && hasType(parent) && typeOf(parent) == EntryType::Playlist)
            Q_EMIT playRequested(EntryType::Playlist, pathOf(parent), parent->indexOfChild(item));
        else
            Q_EMIT playRequested(typeOf(item), pathOf(item));
    });
    connect(m_view, &LibraryView::removeRequested, this, &LibraryPanel::removeItem);
    connect(m_view, &QTreeWidget::currentItemChanged, this, &LibraryPanel::updateButtons);
    connect(addFolder, &QToolButton::clicked, this, &LibraryPanel::addFolderRequested);
    connect(addPlaylist, &QToolButton::clicked, this, &LibraryPanel::addPlaylistFileRequested);
    connect(saveQueue, &QToolButton::clicked, this, &LibraryPanel::saveQueueRequested);
    connect(m_removeButton, &QToolButton::clicked, this, [this] { removeItem(m_view->currentItem()); });

    rebuild();
}

void LibraryPanel::setLibrary(MediaLibrary *library)
{
    if (m_library)
        disconnect(m_library, nullptr, this, nullptr);
    m_library = library;
    if (m_library)
        connect(m_library, &MediaLibrary::changed, this, &LibraryPanel::rebuild);
    rebuild();
}

void LibraryPanel::rebuild()
{
    const QString current = m_view->currentItem() && hasType(m_view->currentItem()) ? itemKey(m_view->currentItem()) : QString();
    m_view->clear();

    auto addSection = [this](const QString &title) {
        auto *section = new QTreeWidgetItem(m_view, {title});
        section->setFlags(Qt::ItemIsEnabled);
        section->setForeground(0, kMutedColor);
        QFont font = section->font(0);
        font.setBold(true);
        section->setFont(0, font);
        section->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
        return section;
    };
    const QList<MediaLibrary::Item> folders = m_library ? m_library->items(MediaLibrary::Kind::Folder) : QList<MediaLibrary::Item>();
    const QList<MediaLibrary::Item> playlists = m_library ? m_library->items(MediaLibrary::Kind::Playlist) : QList<MediaLibrary::Item>();
    m_folders = addSection(tr("Folders (%1)").arg(folders.size()));
    m_playlists = addSection(tr("Playlists (%1)").arg(playlists.size()));

    for (const MediaLibrary::Item &folder : folders)
        addEntry(m_folders, EntryType::Folder, folder.path, folder.name)->setData(0, kStoredRole, true);
    for (const MediaLibrary::Item &playlist : playlists)
        addEntry(m_playlists, EntryType::Playlist, playlist.path, playlist.name)->setData(0, kStoredRole, true);
    if (folders.isEmpty())
        addPlaceholder(m_folders, tr("Add folders to find them here"));
    if (playlists.isEmpty())
        addPlaceholder(m_playlists, tr("Save the playlist to keep it here"));

    // Expanding lists the children, which re-expand in turn.
    for (QTreeWidgetItem *section : {m_folders, m_playlists}) {
        section->setExpanded(true);
        for (int i = 0; i < section->childCount(); ++i) {
            QTreeWidgetItem *item = section->child(i);
            if (hasType(item) && m_expandedKeys.contains(itemKey(item)))
                item->setExpanded(true);
        }
    }
    if (!current.isEmpty()) {
        for (QTreeWidgetItemIterator it(m_view); *it; ++it) {
            if (hasType(*it) && itemKey(*it) == current) {
                m_view->setCurrentItem(*it);
                break;
            }
        }
    }
    updateButtons();
}

void LibraryPanel::populate(QTreeWidgetItem *item)
{
    if (!hasType(item) || item->data(0, kLoadedRole).toBool())
        return;
    item->setData(0, kLoadedRole, true);
    const QString path = pathOf(item);

    if (typeOf(item) == EntryType::Folder) {
        const QDir dir(path);
        if (!dir.exists()) {
            addPlaceholder(item, tr("Folder not found"));
            return;
        }
        for (const QFileInfo &info : naturallySorted(dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)))
            addEntry(item, EntryType::Folder, info.absoluteFilePath(), info.fileName());
        for (const QFileInfo &info : naturallySorted(dir.entryInfoList(QDir::Files))) {
            const QString file = info.absoluteFilePath();
            if (MediaFiles::isPlaylistFile(file))
                addEntry(item, EntryType::Playlist, file, info.fileName());
            else if (MediaFiles::isMediaFile(file))
                addEntry(item, EntryType::File, file, info.fileName());
        }
        if (item->childCount() == 0)
            addPlaceholder(item, tr("No media files"));
    } else if (typeOf(item) == EntryType::Playlist) {
        const QStringList entries = PlaylistOps::readPlaylist(path);
        for (const QString &entry : entries) {
            const QString local = MediaFiles::localPath(entry);
            addEntry(item, EntryType::File, entry, local.isEmpty() ? entry : QFileInfo(local).fileName());
        }
        if (entries.isEmpty())
            addPlaceholder(item, QFileInfo::exists(path) ? tr("Empty playlist") : tr("Playlist not found"));
    }

    // Subfolders that were open before the tree was rebuilt.
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem *child = item->child(i);
        if (hasType(child) && typeOf(child) != EntryType::File && m_expandedKeys.contains(itemKey(child)))
            child->setExpanded(true);
    }
}

void LibraryPanel::expandItem(QTreeWidgetItem *item)
{
    populate(item);
    item->setExpanded(true);
}

QTreeWidgetItem *LibraryPanel::findItem(EntryType type, const QString &path) const
{
    for (QTreeWidgetItemIterator it(m_view); *it; ++it) {
        if (hasType(*it) && typeOf(*it) == type && pathOf(*it) == path)
            return *it;
    }
    return nullptr;
}

void LibraryPanel::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("LibraryContextMenu"));
    QTreeWidgetItem *item = m_view->itemAt(pos);
    if (hasType(item)) {
        m_view->setCurrentItem(item);
        const EntryType type = typeOf(item);
        const QString path = pathOf(item);
        menu.addAction(skinIcon(IconType::Play), tr("Play"), this, [this, item] { Q_EMIT m_view->itemActivated(item, 0); });
        menu.addAction(skinIcon(IconType::Add), tr("Add to Playlist"), this,
                       [this, type, path] { Q_EMIT queueRequested(type, path); });
        if (type == EntryType::Folder) {
            menu.addAction(tr("Refresh"), this, [this, item] {
                qDeleteAll(item->takeChildren());
                item->setData(0, kLoadedRole, false);
                if (item->isExpanded())
                    populate(item);
            });
        }
        if (item->data(0, kStoredRole).toBool()) {
            menu.addSeparator();
            const QString suffix = QFileInfo(path).suffix().toLower();
            if (type == EntryType::Playlist && (suffix == QLatin1String("m3u") || suffix == QLatin1String("m3u8"))) {
                menu.addAction(tr("Replace with Current Playlist"), this, [this, item, path] {
                    Q_EMIT overwritePlaylistRequested(path);
                    qDeleteAll(item->takeChildren());
                    item->setData(0, kLoadedRole, false);
                    if (item->isExpanded())
                        populate(item);
                });
            }
            menu.addAction(tr("Rename..."), this, [this, item] { renameItem(item); });
            const bool owned = type == EntryType::Playlist && MediaLibrary::ownsPlaylist(path);
            menu.addAction(skinIcon(IconType::Remove), owned ? tr("Delete Playlist") : tr("Remove from Library"), this,
                           [this, item] { removeItem(item); });
        }
        menu.addSeparator();
    }
    menu.addAction(skinIcon(IconType::Folder), tr("Add Folder to Library..."), this, &LibraryPanel::addFolderRequested);
    menu.addAction(skinIcon(IconType::Open), tr("Add Playlist File to Library..."), this, &LibraryPanel::addPlaylistFileRequested);
    menu.addAction(skinIcon(IconType::Add), tr("Save Current Playlist to Library..."), this, &LibraryPanel::saveQueueRequested);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void LibraryPanel::renameItem(QTreeWidgetItem *item)
{
    if (!m_library || !item || !item->data(0, kStoredRole).toBool())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal, item->text(0), &ok);
    if (ok)
        m_library->rename(kindOf(typeOf(item)), pathOf(item), name);
}

void LibraryPanel::removeItem(QTreeWidgetItem *item)
{
    if (!m_library || !item || !item->data(0, kStoredRole).toBool())
        return;
    const EntryType type = typeOf(item);
    const QString path = pathOf(item);
    if (type == EntryType::Playlist && MediaLibrary::ownsPlaylist(path)) {
        const auto answer = QMessageBox::question(this, tr("Delete Playlist"),
                                                  tr("Delete the playlist \"%1\"?").arg(item->text(0)));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_expandedKeys.remove(itemKey(item));
    m_library->remove(kindOf(type), path);
}

void LibraryPanel::updateButtons()
{
    QTreeWidgetItem *item = m_view->currentItem();
    m_removeButton->setEnabled(item && item->data(0, kStoredRole).toBool());
}
