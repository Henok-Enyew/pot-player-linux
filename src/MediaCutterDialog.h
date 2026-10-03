#pragma once

#include "MediaCutter.h"

#include <QDialog>

#include <functional>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressDialog;
class QPushButton;
class QRadioButton;

// Tools -> Cut / Extract Media...: picks a range of the playing file and
// writes it out with ffmpeg, losslessly or as audio only.
class MediaCutterDialog : public QDialog
{
    Q_OBJECT

public:
    struct Setup {
        QString input;          // local file
        QString title;          // media title, for the output name
        double duration = 0;    // seconds, 0 if unknown
        double start = 0;
        double end = 0;
        std::function<double()> currentTime; // the playback position
    };

    explicit MediaCutterDialog(const Setup &setup, QWidget *parent = nullptr);

    MediaCutter::Job job() const;
    MediaCutter *cutter() const { return m_cutter; }
    // Default output: "<title>_clip.<ext>" next to the input (or in
    // ~/Videos when that folder is read-only).
    static QString defaultOutput(const QString &input, const QString &title, const QString &suffix);
    // Why the current inputs can't be exported, or empty.
    QString validationError() const;

public Q_SLOTS:
    void startExport();

Q_SIGNALS:
    void exported(const QString &path);

private:
    void updateState();
    void updateOutputSuffix();
    QString outputSuffix() const;

    Setup m_setup;
    MediaCutter *m_cutter;
    QLineEdit *m_start;
    QLineEdit *m_end;
    QLabel *m_duration;
    QRadioButton *m_copy;
    QRadioButton *m_audio;
    QComboBox *m_audioFormat;
    QLineEdit *m_output;
    QLabel *m_status;
    QPushButton *m_exportButton;
    QProgressDialog *m_progress = nullptr;
    bool m_outputEdited = false;
};
