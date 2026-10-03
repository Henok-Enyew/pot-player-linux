#pragma once

#include "MediaDownloader.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

// Download from URL...: fetches a video or its audio with yt-dlp, or hands
// the URL to mpv to stream.
class MediaDownloaderDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MediaDownloaderDialog(QWidget *parent = nullptr);

    MediaDownloader *downloader() const { return m_downloader; }
    QString url() const;
    MediaDownloader::Format format() const;
    QString directory() const;
    bool playWhenDone() const;
    bool isBusy() const { return m_downloader->isRunning(); }

    // ~/Videos, else ~/Downloads, else the home folder; or the last one used.
    static QString defaultDirectory();

public Q_SLOTS:
    void startDownload();
    void streamDirectly();

Q_SIGNALS:
    // A download finished; `play` is the "Play immediately" choice.
    void downloaded(const QString &path, bool play);
    // Stream `url` in mpv, picking streams with yt-dlp format `format`.
    void streamRequested(const QString &url, const QString &format);

protected:
    void reject() override;

private:
    void updateState();
    void setStatus(const QString &text, bool error = false);
    void setBusy(bool busy);

    MediaDownloader *m_downloader;
    QLineEdit *m_url;
    QLabel *m_site;
    QComboBox *m_format;
    QLineEdit *m_directory;
    QCheckBox *m_play;
    QProgressBar *m_progress;
    QLabel *m_speed;
    QLabel *m_status;
    QLabel *m_warning;
    QPushButton *m_downloadButton;
    QPushButton *m_streamButton;
};
