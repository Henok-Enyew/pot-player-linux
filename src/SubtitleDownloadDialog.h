#pragma once

#include "SubtitleFinder.h"
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

// Settings for subtitle downloads: an optional OpenSubtitles API key (for
// exact matches when the build has none) and where files are saved.
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

// Subtitles -> Download Subtitles...: finds subtitles for the playing file
// without any account or key (podnapisi.net, plus OpenSubtitles' exact
// matches when the build has a key), downloads the chosen one to
// ~/.cache/potplayer-linux/subtitles and hands it to the player.
class SubtitleDownloadDialog : public QDialog
{
    Q_OBJECT

public:
    // `mediaPath` is the playing file (a path or URL); its name seeds the search.
    explicit SubtitleDownloadDialog(const QString &mediaPath, QWidget *parent = nullptr);

    SubtitleFinder *finder() const { return m_finder; }
    const QList<SubtitleResult> &results() const { return m_results; }
    QString movieHash() const { return m_hash; }
    bool isBusy() const;

public Q_SLOTS:
    void searchByHash();
    void searchByName();
    // Downloads the selected result, then emits subtitleDownloaded() and closes.
    void downloadSelected();

Q_SIGNALS:
    // `label` names it for the OSD: "English / The.Matrix.1999.srt".
    void subtitleDownloaded(const QString &path, const QString &label);

protected:
    void reject() override;

private:
    void search(SubtitleFinder::Mode mode);
    void showResults(const QList<SubtitleResult> &results, const QString &note);
    void setBusy(bool busy, const QString &status = {});
    void setStatus(const QString &text, bool error = false);
    void openSettings();
    void updateButtons();

    QString m_mediaPath;
    SubtitleSearch::ParsedName m_parsed;
    QString m_hash;
    SubtitleFinder *m_finder;
    QList<SubtitleResult> m_results;

    QLineEdit *m_query;
    QComboBox *m_language;
    QPushButton *m_hashButton;
    QPushButton *m_nameButton;
    QProgressBar *m_progress;
    QLabel *m_status;
    QTreeWidget *m_table;
    QPushButton *m_settingsButton;
    QPushButton *m_downloadButton;
};
