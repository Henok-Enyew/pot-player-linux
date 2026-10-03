#pragma once

#include "StreamCatalog.h"

#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Browser for live TV channels (iptv-org) and online radio stations
// (Radio-Browser) by country, with a search filter and channel logos.
// Playing and queueing are requests for the main window.
class LiveStreamDialog : public QDialog
{
    Q_OBJECT

public:
    enum Source { Tv, Radio };

    explicit LiveStreamDialog(QWidget *parent = nullptr);

    Source source() const;
    void setSource(Source source);
    // The selected country code; "index" stands for the TV category index.
    QString country() const;
    void setCountry(const QString &code);
    void setFilterText(const QString &text);
    // Shows only stations of `genre` (a group-title or tag); empty for all.
    void setGenre(const QString &genre);
    QString genre() const;

    // Marks a stream that failed to play: greyed out and skipped as an alternative.
    void markUnavailable(const QString &url);
    bool isUnavailable(const QString &url) const { return m_unavailable.contains(url); }
    // Other streams of the same channel in the loaded list (same iptv-org
    // channel ID, any feed), ones that play anywhere and in the best quality
    // first, leaving out ones known not to work.
    QList<StreamCatalog::Station> alternatives(const StreamCatalog::Station &station) const;

    QTreeWidget *view() const { return m_view; }
    // The stations shown (after filtering), in display order.
    QList<StreamCatalog::Station> visibleStations() const;
    bool isLoading() const { return m_loading; }

    // Loads the selected list, from the cache if it is fresh unless `refresh`.
    void reload(bool refresh = false);

Q_SIGNALS:
    void playRequested(const StreamCatalog::Station &station, bool radio);
    void queueRequested(const StreamCatalog::Station &station);

private:
    QString cacheKey() const;
    QUrl sourceUrl() const;
    void fillCountries();
    void fillGenres();
    void onLoaded(const QString &key, const QByteArray &data, const QDateTime &fetched, bool fromCache);
    void onFailed(const QString &key, const QString &error);
    void populate();
    void applyFilter();
    void updateStatus();
    void playCurrent();
    void queueCurrent();
    void showContextMenu(const QPoint &pos);
    const StreamCatalog::Station *stationOf(const QTreeWidgetItem *item) const;

    // Channel logos: requested for visible rows, cached on disk and in memory.
    void requestVisibleLogos();
    void startLogoDownloads();
    void applyLogo(const QString &url, const QIcon &icon);

    StreamFetcher *m_fetcher;
    QTabBar *m_tabs;
    QComboBox *m_country;
    QComboBox *m_genre;
    QLineEdit *m_filter;
    QCheckBox *m_hideGeoBlocked;
    QToolButton *m_refresh;
    QTreeWidget *m_view;
    QLabel *m_status;
    QPushButton *m_play;
    QPushButton *m_queue;

    QList<StreamCatalog::Station> m_stations;
    QString m_loadingKey;
    bool m_loading = false;
    QString m_statusText;
    QSet<QString> m_unavailable; // stream URLs that failed this session

    QIcon m_placeholder;
    QHash<QString, QIcon> m_logos;
    QSet<QString> m_failedLogos;
    QStringList m_logoQueue;
    QSet<QString> m_logoRequests; // queued or downloading
    int m_logoDownloads = 0;
};
