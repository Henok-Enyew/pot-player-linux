#include "SubtitleDownloadDialog.h"
#include "MediaFiles.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

enum Column { LanguageColumn, FileColumn, DownloadsColumn, RatingColumn, FormatColumn, HearingImpairedColumn, ColumnCount };

// Item data: the index into the results, and the value columns sort by.
constexpr int kResultRole = Qt::UserRole;
constexpr int kSortRole = Qt::UserRole + 1;
constexpr int kBadgeRole = Qt::UserRole + 2;

const QColor kAccent = Theme::Accent;
const QColor kErrorColor(0xFF, 0x6B, 0x5B);

// Sorts numeric columns by value instead of text.
class ResultItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(column, kSortRole);
        const QVariant b = other.data(column, kSortRole);
        if (a.isValid() && b.isValid())
            return a.toDouble() < b.toDouble();
        return QString::localeAwareCompare(text(column), other.text(column)) < 0;
    }
};

// Draws a rounded "pill" after the cell text, e.g. "HI" or "Exact match".
class BadgeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QString badge = index.data(kBadgeRole).toString();
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QFontMetrics metrics(opt.font);
        const int badgeWidth = badge.isEmpty() ? 0 : metrics.horizontalAdvance(badge) + 12;
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
        if (badgeWidth > 0 && !opt.text.isEmpty())
            opt.text = metrics.elidedText(opt.text, Qt::ElideMiddle, textRect.width() - badgeWidth - 6);
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        if (badge.isEmpty())
            return;

        const int textWidth = opt.text.isEmpty() ? 0 : metrics.horizontalAdvance(opt.text) + 6;
        const int height = metrics.height() + 2;
        const QRect pill(textRect.left() + textWidth, textRect.center().y() - height / 2 + 1, badgeWidth, height);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(kAccent);
        painter->drawRoundedRect(pill, height / 2.0, height / 2.0);
        QFont font = opt.font;
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(Theme::Surface);
        painter->drawText(pill, Qt::AlignCenter, badge);
        painter->restore();
    }
};

} // namespace

SubtitleSettingsDialog::SubtitleSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_apiKey(new QLineEdit(this))
    , m_besideVideo(new QCheckBox(tr("Save subtitles next to the video"), this))
{
    setWindowTitle(tr("Subtitle Download Settings"));
    setObjectName(QStringLiteral("SubtitleSettingsDialog"));
    m_apiKey->setObjectName(QStringLiteral("ApiKeyEdit"));
    m_apiKey->setText(SubtitleSearch::userApiKey());
    m_apiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_apiKey->setPlaceholderText(SubtitleSearch::builtInApiKey().isEmpty()
                                     ? tr("Required: your OpenSubtitles API key")
                                     : tr("Optional: the built-in key is used otherwise"));
    m_apiKey->setMinimumWidth(320);
    m_besideVideo->setChecked(SubtitleSearch::saveBesideVideo());
    m_besideVideo->setToolTip(tr("Otherwise, or when the video's folder is read-only, they go to %1")
                                  .arg(SubtitleSearch::cacheDir()));

    auto *help = new QLabel(tr("Create a free API key at "
                               "<a href=\"https://www.opensubtitles.com/consumers\">opensubtitles.com/consumers</a>."),
                            this);
    help->setOpenExternalLinks(true);
    help->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("API key:"), m_apiKey);
    form->addRow(QString(), help);
    form->addRow(QString(), m_besideVideo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void SubtitleSettingsDialog::accept()
{
    SubtitleSearch::setUserApiKey(m_apiKey->text());
    SubtitleSearch::setSaveBesideVideo(m_besideVideo->isChecked());
    QDialog::accept();
}

SubtitleDownloadDialog::SubtitleDownloadDialog(const QString &mediaPath, QWidget *parent)
    : QDialog(parent)
    , m_mediaPath(mediaPath)
    , m_parsed(SubtitleSearch::parseFileName(mediaPath))
    , m_client(new OpenSubtitlesClient(this))
    , m_query(new QLineEdit(this))
    , m_language(new QComboBox(this))
    , m_matchHash(new QCheckBox(tr("Match this exact file (movie hash)"), this))
    , m_searchButton(new QPushButton(tr("Search"), this))
    , m_settingsButton(new QPushButton(tr("Settings..."), this))
    , m_progress(new QProgressBar(this))
    , m_status(new QLabel(this))
    , m_table(new QTreeWidget(this))
    , m_downloadButton(new QPushButton(tr("Download && Apply"), this))
{
    setObjectName(QStringLiteral("SubtitleDownloadDialog"));
    setWindowTitle(tr("Download Subtitles"));
    resize(760, 460);

    m_query->setObjectName(QStringLiteral("SubtitleQuery"));
    m_query->setPlaceholderText(tr("Movie or show title"));
    m_query->setClearButtonEnabled(true);
    m_query->setText(m_parsed.title);

    m_language->setObjectName(QStringLiteral("SubtitleLanguage"));
    for (const SubtitleSearch::Language &language : SubtitleSearch::languages())
        m_language->addItem(language.name, language.code);
    m_language->addItem(tr("All Languages"), QString());
    m_language->setCurrentIndex(std::max(0, m_language->findData(SubtitleSearch::language())));

    // The hash identifies the exact release, so matching subtitles are in sync.
    const QString local = MediaFiles::localPath(mediaPath);
    if (!local.isEmpty())
        m_hash = SubtitleSearch::movieHash(local);
    m_matchHash->setObjectName(QStringLiteral("SubtitleMatchHash"));
    m_matchHash->setEnabled(!m_hash.isEmpty());
    m_matchHash->setChecked(!m_hash.isEmpty());
    if (!m_hash.isEmpty())
        m_matchHash->setToolTip(tr("Movie hash %1").arg(m_hash));

    m_searchButton->setObjectName(QStringLiteral("SubtitleSearchButton"));
    m_searchButton->setAutoDefault(false);
    m_settingsButton->setObjectName(QStringLiteral("SubtitleSettingsButton"));
    m_settingsButton->setAutoDefault(false);

    // A thin, indeterminate bar while a request runs.
    m_progress->setObjectName(QStringLiteral("SubtitleProgress"));
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4);
    m_progress->setVisible(false);
    m_status->setObjectName(QStringLiteral("SubtitleStatus"));

    m_table->setObjectName(QStringLiteral("SubtitleResults"));
    m_table->setColumnCount(ColumnCount);
    m_table->setHeaderLabels({tr("Language"), tr("Subtitle File Name"), tr("Downloads"), tr("Rating"), tr("Format"),
                              tr("HI")});
    m_table->headerItem()->setToolTip(HearingImpairedColumn, tr("For the hearing impaired"));
    m_table->setRootIsDecorated(false);
    m_table->setUniformRowHeights(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setItemDelegate(new BadgeDelegate(m_table));
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(-1, Qt::AscendingOrder); // keep the server's order until a header is clicked
    QHeaderView *header = m_table->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(FileColumn, QHeaderView::Stretch);

    m_downloadButton->setObjectName(QStringLiteral("SubtitleDownloadButton"));
    m_downloadButton->setDefault(true);
    auto *close = new QPushButton(tr("Close"), this);
    close->setAutoDefault(false);

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(m_query, 1);
    searchRow->addWidget(m_language);
    searchRow->addWidget(m_searchButton);
    auto *optionsRow = new QHBoxLayout;
    optionsRow->addWidget(m_matchHash);
    optionsRow->addStretch();
    optionsRow->addWidget(m_settingsButton);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_status, 1);
    buttonRow->addWidget(m_downloadButton);
    buttonRow->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(searchRow);
    layout->addLayout(optionsRow);
    layout->addWidget(m_progress);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttonRow);

    connect(m_searchButton, &QPushButton::clicked, this, &SubtitleDownloadDialog::search);
    connect(m_query, &QLineEdit::returnPressed, this, &SubtitleDownloadDialog::search);
    connect(m_settingsButton, &QPushButton::clicked, this, &SubtitleDownloadDialog::openSettings);
    connect(m_downloadButton, &QPushButton::clicked, this, &SubtitleDownloadDialog::downloadSelected);
    connect(close, &QPushButton::clicked, this, &SubtitleDownloadDialog::reject);
    connect(m_table, &QTreeWidget::itemSelectionChanged, this, &SubtitleDownloadDialog::updateButtons);
    connect(m_table, &QTreeWidget::itemActivated, this, &SubtitleDownloadDialog::downloadSelected);
    connect(m_client, &OpenSubtitlesClient::searchFinished, this, &SubtitleDownloadDialog::showResults);
    connect(m_client, &OpenSubtitlesClient::downloadFinished, this, [this](const QString &path) {
        setBusy(false);
        Q_EMIT subtitleDownloaded(path);
        accept();
    });
    connect(m_client, &OpenSubtitlesClient::failed, this, [this](const QString &message) {
        setBusy(false);
        setStatus(message, true);
    });

    updateButtons();
    // Search right away when there is something to search for.
    if (SubtitleSearch::apiKey().isEmpty())
        setStatus(tr("Add your OpenSubtitles API key in Settings to search."), true);
    else if (!m_query->text().isEmpty() || !m_hash.isEmpty())
        QTimer::singleShot(0, this, &SubtitleDownloadDialog::search);
}

bool SubtitleDownloadDialog::isBusy() const
{
    return m_client->isBusy();
}

void SubtitleDownloadDialog::search()
{
    const QString key = SubtitleSearch::apiKey();
    if (key.isEmpty()) {
        setStatus(tr("Add your OpenSubtitles API key in Settings to search."), true);
        return;
    }
    const QString text = m_query->text().trimmed();
    const bool useHash = m_matchHash->isChecked() && !m_hash.isEmpty();
    if (text.isEmpty() && !useHash) {
        setStatus(tr("Enter a title to search for."), true);
        return;
    }

    OpenSubtitlesClient::Query query;
    query.text = text;
    if (const QString code = m_language->currentData().toString(); !code.isEmpty())
        query.languages = {code};
    if (useHash)
        query.movieHash = m_hash;
    // The year and episode come from the file name; they don't apply to a
    // title the user typed.
    if (text == m_parsed.title) {
        query.year = m_parsed.year;
        query.season = m_parsed.season;
        query.episode = m_parsed.episode;
    }
    SubtitleSearch::setLanguage(m_language->currentData().toString());

    m_client->setApiKey(key);
    m_table->clear();
    m_results.clear();
    setBusy(true, tr("Searching OpenSubtitles..."));
    m_client->search(query);
}

void SubtitleDownloadDialog::showResults(const QList<OpenSubtitlesClient::Result> &results)
{
    setBusy(false);
    m_results = results;
    m_table->setSortingEnabled(false);
    m_table->clear();
    for (int i = 0; i < results.size(); ++i) {
        const OpenSubtitlesClient::Result &result = results[i];
        auto *item = new ResultItem(m_table);
        item->setData(0, kResultRole, i);
        item->setText(LanguageColumn, SubtitleSearch::languageName(result.language));
        item->setText(FileColumn, result.fileName);
        QString tip = result.release;
        if (!result.uploader.isEmpty())
            tip += tr("\nUploaded by %1").arg(result.uploader);
        if (result.machineTranslated)
            tip += tr("\nMachine translated");
        item->setToolTip(FileColumn, tip.trimmed());
        if (result.hashMatch) {
            item->setData(FileColumn, kBadgeRole, tr("Exact match"));
            item->setToolTip(FileColumn, tr("Made for this exact file, so it is in sync.\n") + tip.trimmed());
        }
        item->setText(DownloadsColumn, QLocale().toString(result.downloads));
        item->setData(DownloadsColumn, kSortRole, result.downloads);
        item->setTextAlignment(DownloadsColumn, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(RatingColumn, result.rating > 0 ? QString::number(result.rating, 'f', 1) : QStringLiteral("–"));
        item->setData(RatingColumn, kSortRole, result.rating);
        item->setTextAlignment(RatingColumn, Qt::AlignCenter);
        item->setText(FormatColumn, QLatin1Char('.') + result.format);
        item->setTextAlignment(FormatColumn, Qt::AlignCenter);
        if (result.hearingImpaired)
            item->setData(HearingImpairedColumn, kBadgeRole, QStringLiteral("HI"));
        item->setData(HearingImpairedColumn, kSortRole, result.hearingImpaired ? 1 : 0);
    }
    m_table->setSortingEnabled(true);
    if (m_table->topLevelItemCount() > 0)
        m_table->setCurrentItem(m_table->topLevelItem(0));
    setStatus(results.isEmpty() ? tr("No subtitles found. Try another title or language.")
                                : tr("%n subtitle(s) found", nullptr, static_cast<int>(results.size())));
    updateButtons();
}

void SubtitleDownloadDialog::downloadSelected()
{
    QTreeWidgetItem *item = m_table->currentItem();
    if (!item || isBusy())
        return;
    const int index = item->data(0, kResultRole).toInt();
    if (index < 0 || index >= m_results.size())
        return;
    const OpenSubtitlesClient::Result &result = m_results[index];
    const QString path = SubtitleSearch::savePath(MediaFiles::localPath(m_mediaPath), result.language, result.format,
                                                  SubtitleSearch::saveBesideVideo());
    m_client->setApiKey(SubtitleSearch::apiKey());
    setBusy(true, tr("Downloading %1...").arg(result.fileName));
    m_client->download(result, path);
}

void SubtitleDownloadDialog::reject()
{
    // Esc first stops a running request, then closes.
    if (isBusy()) {
        m_client->cancel();
        setBusy(false);
        setStatus(tr("Cancelled."));
        return;
    }
    QDialog::reject();
}

void SubtitleDownloadDialog::setBusy(bool busy, const QString &status)
{
    m_progress->setVisible(busy);
    m_query->setEnabled(!busy);
    m_language->setEnabled(!busy);
    m_searchButton->setEnabled(!busy);
    m_settingsButton->setEnabled(!busy);
    m_table->setEnabled(!busy);
    if (!status.isEmpty())
        setStatus(status);
    updateButtons();
}

void SubtitleDownloadDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    QPalette palette = m_status->palette();
    palette.setColor(QPalette::WindowText, error ? kErrorColor : QApplication::palette().color(QPalette::WindowText));
    m_status->setPalette(palette);
}

void SubtitleDownloadDialog::openSettings()
{
    SubtitleSettingsDialog settings(this);
    if (settings.exec() == QDialog::Accepted && !SubtitleSearch::apiKey().isEmpty() && m_results.isEmpty())
        search();
}

void SubtitleDownloadDialog::updateButtons()
{
    m_downloadButton->setEnabled(!isBusy() && m_table->currentItem() && !m_table->selectedItems().isEmpty());
}
