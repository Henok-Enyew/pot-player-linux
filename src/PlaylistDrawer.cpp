#include "PlaylistDrawer.h"
#include "Icons.h"
#include "MediaFiles.h"
#include "MpvWidget.h"
#include "PlaylistSession.h"
#include "Theme.h"
#include "TimeFormat.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kDrawerWidth = 280;
constexpr int kAnimationMs = 180;
const QColor kPlayingColor = Theme::Accent;
const QColor kDurationColor = Theme::TextSecondary;
// Item data: the entry's filename, and its duration text.
constexpr int kFilenameRole = Qt::UserRole;
constexpr int kDurationRole = Qt::UserRole + 1;
constexpr int kDurationGap = 8;

QStringList mediaFiles(const QMimeData *mime)
{
    QStringList files;
    for (const QUrl &url : mime->urls()) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (url.isLocalFile() && QFileInfo(path).isDir())
            files.append(MediaFiles::mediaFilesInFolder(path));
        else if (!url.isLocalFile() || !MpvWidget::isSubtitleFile(path))
            files.append(path);
    }
    return files;
}

// Short duration text: m:ss below an hour, h:mm:ss above.
QString durationText(double seconds)
{
    if (seconds < 0)
        return {};
    const QString full = formatTime(seconds);
    if (seconds >= 3600)
        return full.startsWith(QLatin1Char('0')) ? full.mid(1) : full;
    const QString minutes = full.mid(3);
    return minutes.startsWith(QLatin1Char('0')) ? minutes.mid(1) : minutes;
}

// Draws the entry's duration right-aligned, eliding the name before it.
class PlaylistItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString duration = index.data(kDurationRole).toString();
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
        int durationWidth = 0;
        if (!duration.isEmpty()) {
            durationWidth = opt.fontMetrics.horizontalAdvance(duration) + kDurationGap;
            opt.text = opt.fontMetrics.elidedText(opt.text, opt.textElideMode, textRect.width() - durationWidth);
        }
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        if (duration.isEmpty())
            return;
        painter->save();
        const bool selected = opt.state & QStyle::State_Selected;
        painter->setPen(selected ? opt.palette.color(QPalette::HighlightedText) : kDurationColor);
        painter->setFont(opt.font);
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, duration);
        painter->restore();
    }
};

} // namespace

PlaylistView::PlaylistView(QWidget *parent)
    : QListWidget(parent)
{
    setObjectName(QStringLiteral("PlaylistView"));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setAcceptDrops(true);
    setTextElideMode(Qt::ElideMiddle);
    setUniformItemSizes(true);
}

QList<int> PlaylistView::selectedVisibleRows() const
{
    // Select All also selects the rows the filter hides; leave those alone.
    QList<int> rows;
    for (const QModelIndex &index : selectionModel()->selectedRows()) {
        if (!isRowHidden(index.row()))
            rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void PlaylistView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this || event->mimeData()->hasUrls())
        QListWidget::dragEnterEvent(event);
    else
        event->ignore();
}

void PlaylistView::dragMoveEvent(QDragMoveEvent *event)
{
    QListWidget::dragMoveEvent(event);
    // External file drops are copies; QListWidget only accepts its own item data.
    if (event->source() != this && event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

int PlaylistView::dropRow(QDropEvent *event) const
{
    const QModelIndex index = indexAt(event->position().toPoint());
    if (!index.isValid())
        return -1;
    switch (dropIndicatorPosition()) {
    case QAbstractItemView::BelowItem:
        return index.row() + 1;
    case QAbstractItemView::OnItem:
        // Dropping on an item inserts before or after it depending on the half.
        return visualRect(index).center().y() < event->position().y() ? index.row() + 1 : index.row();
    case QAbstractItemView::AboveItem:
        return index.row();
    case QAbstractItemView::OnViewport:
        break;
    }
    return -1;
}

void PlaylistView::dropEvent(QDropEvent *event)
{
    const int row = dropRow(event);
    if (event->source() == this) {
        // Move the dragged entries one by one, keeping their order.
        const QList<int> rows = selectedVisibleRows();
        int target = row < 0 ? count() : row;
        int shift = 0; // entries above the target that were already moved below it
        for (int from : std::as_const(rows)) {
            const int current = from < target ? from - shift : from;
            Q_EMIT moveRequested(current, target);
            if (from < target)
                ++shift;
            else
                ++target;
        }
    } else {
        const QStringList files = mediaFiles(event->mimeData());
        if (!files.isEmpty())
            Q_EMIT filesDropped(files, row);
    }
    // The playlist is rebuilt from mpv, so the view must not move anything itself.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
}

void PlaylistView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        const QList<int> rows = selectedVisibleRows();
        if (!rows.isEmpty())
            Q_EMIT removeRequested(rows);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (currentRow() >= 0)
            Q_EMIT itemActivated(currentItem());
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

PlaylistDrawer::PlaylistDrawer(QWidget *parent)
    : QFrame(parent)
    , m_view(new PlaylistView(this))
    , m_filter(new QLineEdit(this))
    , m_countLabel(new QLabel(this))
    , m_animation(new QPropertyAnimation(this, "drawerWidth", this))
{
    setObjectName(QStringLiteral("PlaylistDrawer"));
    m_view->setItemDelegate(new PlaylistItemDelegate(m_view));
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    buildMenus();

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

    // Title and the editing buttons.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(10, 6, 6, 2);
    header->setSpacing(0);
    auto *title = new QLabel(tr("Playlist"), this);
    title->setObjectName(QStringLiteral("PlaylistTitle"));
    m_countLabel->setObjectName(QStringLiteral("PlaylistCount"));
    header->addWidget(title);
    header->addSpacing(4);
    header->addWidget(m_countLabel);
    header->addStretch();
    QToolButton *add = makeButton(IconType::Add, tr("Add Files..."), "PlaylistAddButton");
    QToolButton *addFolder = makeButton(IconType::Folder, tr("Add Folder..."), "PlaylistAddFolderButton");
    QToolButton *remove = makeButton(IconType::Remove, tr("Remove Selected (Del)"), "PlaylistRemoveButton");
    QToolButton *clear = makeButton(IconType::Clear, tr("Clear Playlist"), "PlaylistClearButton");
    for (QToolButton *button : {add, addFolder, remove, clear})
        header->addWidget(button);

    // Search field and the arranging buttons.
    auto *tools = new QHBoxLayout;
    tools->setContentsMargins(8, 2, 6, 6);
    tools->setSpacing(2);
    m_filter->setObjectName(QStringLiteral("PlaylistFilter"));
    m_filter->setPlaceholderText(tr("Search playlist"));
    m_filter->setClearButtonEnabled(true);
    m_filter->addAction(skinIcon(IconType::Search), QLineEdit::LeadingPosition);
    m_filter->installEventFilter(this);
    tools->addWidget(m_filter, 1);
    QToolButton *shuffle = makeButton(IconType::Shuffle, tr("Shuffle"), "PlaylistShuffleButton");
    QToolButton *sort = makeButton(IconType::Sort, tr("Sort"), "PlaylistSortButton");
    QToolButton *more = makeButton(IconType::More, tr("More"), "PlaylistMenuButton");
    sort->setMenu(m_sortMenu);
    more->setMenu(m_moreMenu);
    for (QToolButton *button : {sort, more}) {
        button->setPopupMode(QToolButton::InstantPopup);
        button->setProperty("hideMenuIndicator", true);
    }
    for (QToolButton *button : {shuffle, sort, more})
        tools->addWidget(button);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addLayout(tools);
    layout->addWidget(m_view, 1);

    connect(m_view, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { Q_EMIT playRequested(m_view->row(item)); });
    connect(m_view, &PlaylistView::moveRequested, this, &PlaylistDrawer::moveRequested);
    connect(m_view, &PlaylistView::filesDropped, this, &PlaylistDrawer::filesDropped);
    connect(m_view, &PlaylistView::removeRequested, this, &PlaylistDrawer::removeRequested);
    connect(m_view, &QWidget::customContextMenuRequested, this, &PlaylistDrawer::showContextMenu);
    connect(m_filter, &QLineEdit::textChanged, this, &PlaylistDrawer::applyFilter);
    connect(add, &QToolButton::clicked, this, &PlaylistDrawer::addRequested);
    connect(addFolder, &QToolButton::clicked, this, &PlaylistDrawer::addFolderRequested);
    connect(clear, &QToolButton::clicked, this, &PlaylistDrawer::clearRequested);
    connect(shuffle, &QToolButton::clicked, this, &PlaylistDrawer::shuffleRequested);
    connect(remove, &QToolButton::clicked, this, [this] {
        const QList<int> rows = m_view->selectedVisibleRows();
        if (!rows.isEmpty())
            Q_EMIT removeRequested(rows);
    });

    m_animation->setDuration(kAnimationMs);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, [this] {
        if (!m_expanded)
            hide();
    });

    setDrawerWidth(0);
    hide();
    setEntries({});
}

void PlaylistDrawer::buildMenus()
{
    using PlaylistOps::SortKey;
    m_sortMenu = new QMenu(tr("Sort"), this);
    m_sortMenu->setObjectName(QStringLiteral("PlaylistSortMenu"));
    auto addSort = [this](const QString &text, SortKey key, bool ascending) {
        m_sortMenu->addAction(text, this, [this, key, ascending] { Q_EMIT sortRequested(key, ascending); });
    };
    addSort(tr("By Name (A to Z)"), SortKey::Name, true);
    addSort(tr("By Name (Z to A)"), SortKey::Name, false);
    m_sortMenu->addSeparator();
    addSort(tr("By Duration (Shortest First)"), SortKey::Duration, true);
    addSort(tr("By Duration (Longest First)"), SortKey::Duration, false);
    m_sortMenu->addSeparator();
    addSort(tr("By File Path"), SortKey::Path, true);
    addSort(tr("By File Size (Smallest First)"), SortKey::Size, true);
    addSort(tr("By File Size (Largest First)"), SortKey::Size, false);
    m_sortMenu->addSeparator();
    m_sortMenu->addAction(tr("Reverse Order"), this, &PlaylistDrawer::reverseRequested);
    m_sortMenu->addAction(skinIcon(IconType::Shuffle), tr("Shuffle"), this, &PlaylistDrawer::shuffleRequested);

    m_moreMenu = new QMenu(this);
    m_moreMenu->setObjectName(QStringLiteral("PlaylistMoreMenu"));
    m_moreMenu->addAction(tr("Open Playlist..."), this, &PlaylistDrawer::openPlaylistRequested);
    m_moreMenu->addAction(tr("Save Playlist..."), this, &PlaylistDrawer::savePlaylistRequested);
    m_moreMenu->addSeparator();
    m_moreMenu->addAction(tr("Remove Missing/Inaccessible Files"), this, &PlaylistDrawer::removeMissingRequested);
    m_moreMenu->addAction(tr("Remove Duplicates"), this, &PlaylistDrawer::removeDuplicatesRequested);
    m_moreMenu->addAction(skinIcon(IconType::Clear), tr("Clear Playlist"), this, &PlaylistDrawer::clearRequested);
    m_moreMenu->addSeparator();
    m_rememberAction = m_moreMenu->addAction(tr("Remember Playlist on Exit"));
    m_rememberAction->setCheckable(true);
    m_resumeAction = m_moreMenu->addAction(tr("Resume Playback Position"));
    m_resumeAction->setCheckable(true);
    connect(m_moreMenu, &QMenu::aboutToShow, this, &PlaylistDrawer::syncOptions);
    // triggered, unlike toggled, only fires for the user's clicks.
    connect(m_rememberAction, &QAction::triggered, this, [this](bool on) {
        PlaylistSession::setRememberPlaylist(on);
        m_resumeAction->setEnabled(on);
    });
    connect(m_resumeAction, &QAction::triggered, this, &PlaylistSession::setResumePlayback);
}

void PlaylistDrawer::syncOptions()
{
    m_rememberAction->setChecked(PlaylistSession::rememberPlaylist());
    m_resumeAction->setChecked(PlaylistSession::resumePlayback());
    m_resumeAction->setEnabled(m_rememberAction->isChecked());
}

void PlaylistDrawer::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("PlaylistContextMenu"));
    const QList<int> rows = m_view->selectedVisibleRows();
    QListWidgetItem *item = m_view->itemAt(pos);
    if (item) {
        const int row = m_view->row(item);
        menu.addAction(skinIcon(IconType::Play), tr("Play"), this, [this, row] { Q_EMIT playRequested(row); });
    }
    QAction *remove = menu.addAction(skinIcon(IconType::Remove), tr("Remove Selected"), this,
                                     [this, rows] { Q_EMIT removeRequested(rows); });
    remove->setShortcut(QKeySequence(Qt::Key_Delete));
    remove->setEnabled(!rows.isEmpty());
    menu.addSeparator();
    menu.addAction(skinIcon(IconType::Add), tr("Add Files..."), this, &PlaylistDrawer::addRequested);
    menu.addAction(skinIcon(IconType::Folder), tr("Add Folder..."), this, &PlaylistDrawer::addFolderRequested);
    menu.addSeparator();
    menu.addMenu(m_sortMenu)->setIcon(skinIcon(IconType::Sort));
    menu.addSeparator();
    // Cleanup, persistence and options are shared with the "More" button.
    for (QAction *action : m_moreMenu->actions())
        menu.addAction(action);
    syncOptions();
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

bool PlaylistDrawer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_filter && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        // Keys the search field uses itself, which the player would otherwise take as hotkeys.
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool handled = key->key() == Qt::Key_Down || key->key() == Qt::Key_Escape
            || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (!handled || key->modifiers() & ~Qt::KeypadModifier)
            return QFrame::eventFilter(watched, event);
        event->accept();
        if (event->type() == QEvent::ShortcutOverride)
            return true;
        if (key->key() == Qt::Key_Escape) {
            m_filter->clear();
            return true;
        }
        // Down or Return: continue in the list, at the first match.
        for (int row = 0; row < m_view->count(); ++row) {
            if (!m_view->isRowHidden(row)) {
                m_view->setCurrentRow(row);
                m_view->setFocus();
                break;
            }
        }
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

void PlaylistDrawer::setEntries(const QVariantList &playlist, const QList<double> &durations)
{
    const int previousRow = m_view->currentRow();
    m_view->clear();
    for (int i = 0; i < playlist.size(); ++i) {
        const QVariantMap entry = playlist[i].toMap();
        const QString filename = entry.value(QStringLiteral("filename")).toString();
        const QString name = PlaylistOps::displayName({filename, entry.value(QStringLiteral("title")).toString()});

        auto *item = new QListWidgetItem(QStringLiteral("%1. %2").arg(i + 1).arg(name), m_view);
        item->setToolTip(filename);
        item->setData(kFilenameRole, filename);
        if (i < durations.size())
            item->setData(kDurationRole, durationText(durations[i]));
        if (entry.value(QStringLiteral("current")).toBool()) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setForeground(kPlayingColor);
        }
    }
    if (previousRow >= 0 && previousRow < m_view->count())
        m_view->setCurrentRow(previousRow, QItemSelectionModel::NoUpdate);
    applyFilter();
}

void PlaylistDrawer::setDurations(const QList<double> &durations)
{
    for (int row = 0; row < m_view->count() && row < durations.size(); ++row) {
        QListWidgetItem *item = m_view->item(row);
        const QString text = durationText(durations[row]);
        if (item->data(kDurationRole).toString() != text)
            item->setData(kDurationRole, text);
    }
}

void PlaylistDrawer::setFilterText(const QString &text)
{
    m_filter->setText(text);
}

QString PlaylistDrawer::filterText() const
{
    return m_filter->text();
}

void PlaylistDrawer::applyFilter()
{
    const QStringList words = m_filter->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int row = 0; row < m_view->count(); ++row) {
        const QListWidgetItem *item = m_view->item(row);
        // The text starts with the row number, which is not part of the name.
        const QString name = item->text().section(QLatin1Char(' '), 1);
        const QString path = item->data(kFilenameRole).toString();
        const bool matches = std::all_of(words.cbegin(), words.cend(), [&](const QString &word) {
            return name.contains(word, Qt::CaseInsensitive) || path.contains(word, Qt::CaseInsensitive);
        });
        m_view->setRowHidden(row, !matches);
    }
    updateCount();
}

void PlaylistDrawer::updateCount()
{
    const int total = m_view->count();
    if (m_filter->text().trimmed().isEmpty()) {
        m_countLabel->setText(QStringLiteral("(%1)").arg(total));
        return;
    }
    int shown = 0;
    for (int row = 0; row < total; ++row)
        shown += m_view->isRowHidden(row) ? 0 : 1;
    m_countLabel->setText(QStringLiteral("(%1/%2)").arg(shown).arg(total));
}

void PlaylistDrawer::setExpanded(bool expanded, bool animate)
{
    if (expanded == m_expanded)
        return;
    m_expanded = expanded;
    m_animation->stop();
    if (expanded)
        show();
    if (animate) {
        m_animation->setStartValue(width());
        m_animation->setEndValue(expanded ? kDrawerWidth : 0);
        m_animation->start();
    } else {
        setDrawerWidth(expanded ? kDrawerWidth : 0);
        setVisible(expanded);
    }
    Q_EMIT expandedChanged(expanded);
}

void PlaylistDrawer::setDrawerWidth(int width)
{
    setFixedWidth(width);
}
