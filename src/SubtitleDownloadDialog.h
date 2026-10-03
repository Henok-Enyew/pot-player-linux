#pragma once

#include "OpenSubtitlesClient.h"
#include "SubtitleSearch.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;

// Settings for subtitle downloads: the OpenSubtitles API key and where
// downloaded files are saved.
class SubtitleSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SubtitleSettingsDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    QLineEdit *m_apiKey;
    QCheckBox *m_besideVideo;
};

// Searches OpenSubtitles.com for subtitles to the playing file, lists the
// results, and downloads the chosen one (Download & Apply).
class SubtitleDownloadDialog : public QDialog
{
    Q_OBJECT

public:
    // `mediaPath` is the playing file (a path or URL); its name seeds the search.
    explicit SubtitleDownloadDialog(const QString &mediaPath, QWidget *parent = nullptr);

    OpenSubtitlesClient *client() const { return m_client; }
    const QList<OpenSubtitlesClient::Result> &results() const { return m_results; }
    QString movieHash() const { return m_hash; }
    // Starts a search with the current inputs.
    void search();
    // Downloads the selected result, then emits subtitleDownloaded() and closes.
    void downloadSelected();
    bool isBusy() const;

Q_SIGNALS:
    void subtitleDownloaded(const QString &path);

protected:
    void reject() override;

private:
    void showResults(const QList<OpenSubtitlesClient::Result> &results);
    void setBusy(bool busy, const QString &status = {});
    void setStatus(const QString &text, bool error = false);
    void openSettings();
    void updateButtons();

    QString m_mediaPath;
    SubtitleSearch::ParsedName m_parsed;
    QString m_hash;
    OpenSubtitlesClient *m_client;
    QList<OpenSubtitlesClient::Result> m_results;

    QLineEdit *m_query;
    QComboBox *m_language;
    QCheckBox *m_matchHash;
    QPushButton *m_searchButton;
    QPushButton *m_settingsButton;
    QProgressBar *m_progress;
    QLabel *m_status;
    QTreeWidget *m_table;
    QPushButton *m_downloadButton;
};
