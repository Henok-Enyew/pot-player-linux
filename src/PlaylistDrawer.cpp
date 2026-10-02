#include "PlaylistDrawer.h"
#include "Icons.h"
#include "MpvWidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kDrawerWidth = 280;
constexpr int kAnimationMs = 180;
const QColor kPlayingColor(0xFF, 0xB4, 0x1E);

QStringList mediaFiles(const QMimeData *mime)
{
    QStringList files;
    for (const QUrl &url : mime->urls()) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!url.isLocalFile() || !MpvWidget::isSubtitleFile(path))
            files.append(path);
    }
    return files;
}

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
        QList<int> rows;
        for (const QModelIndex &index : selectionModel()->selectedRows())
            rows.append(index.row());
        std::sort(rows.begin(), rows.end());
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
        QList<int> rows;
        for (const QModelIndex &index : selectionModel()->selectedRows())
            rows.append(index.row());
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
    , m_countLabel(new QLabel(this))
    , m_animation(new QPropertyAnimation(this, "drawerWidth", this))
{
    setObjectName(QStringLiteral("PlaylistDrawer"));

    auto *header = new QHBoxLayout;
    header->setContentsMargins(10, 6, 6, 6);
    auto *title = new QLabel(tr("Playlist"), this);
    title->setObjectName(QStringLiteral("PlaylistTitle"));
    m_countLabel->setObjectName(QStringLiteral("PlaylistCount"));
    header->addWidget(title);
    header->addWidget(m_countLabel);
    header->addStretch();

    auto makeButton = [this](IconType icon, const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setIcon(skinIcon(icon));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    QToolButton *add = makeButton(IconType::Add, tr("Add Files..."));
    QToolButton *remove = makeButton(IconType::Remove, tr("Remove Selected (Del)"));
    QToolButton *clear = makeButton(IconType::Clear, tr("Clear Playlist"));
    header->addWidget(add);
    header->addWidget(remove);
    header->addWidget(clear);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_view, 1);

    connect(m_view, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { Q_EMIT playRequested(m_view->row(item)); });
    connect(m_view, &PlaylistView::moveRequested, this, &PlaylistDrawer::moveRequested);
    connect(m_view, &PlaylistView::filesDropped, this, &PlaylistDrawer::filesDropped);
    connect(m_view, &PlaylistView::removeRequested, this, &PlaylistDrawer::removeRequested);
    connect(add, &QToolButton::clicked, this, &PlaylistDrawer::addRequested);
    connect(clear, &QToolButton::clicked, this, &PlaylistDrawer::clearRequested);
    connect(remove, &QToolButton::clicked, this, [this] {
        QList<int> rows;
        for (const QModelIndex &index : m_view->selectionModel()->selectedRows())
            rows.append(index.row());
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

void PlaylistDrawer::setEntries(const QVariantList &playlist)
{
    const int previousRow = m_view->currentRow();
    m_view->clear();
    for (int i = 0; i < playlist.size(); ++i) {
        const QVariantMap entry = playlist[i].toMap();
        const QString filename = entry.value(QStringLiteral("filename")).toString();
        QString name = entry.value(QStringLiteral("title")).toString();
        if (name.isEmpty()) {
            const QUrl url(filename);
            name = url.scheme().size() > 1 ? filename : QFileInfo(filename).fileName();
        }

        auto *item = new QListWidgetItem(QStringLiteral("%1. %2").arg(i + 1).arg(name), m_view);
        item->setToolTip(filename);
        if (entry.value(QStringLiteral("current")).toBool()) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setForeground(kPlayingColor);
        }
    }
    if (previousRow >= 0 && previousRow < m_view->count())
        m_view->setCurrentRow(previousRow, QItemSelectionModel::NoUpdate);
    m_countLabel->setText(QStringLiteral("(%1)").arg(playlist.size()));
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
