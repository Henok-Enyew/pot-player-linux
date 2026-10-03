#include "MediaCutterDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const QColor kErrorColor(0xFF, 0x6B, 0x5B);

// A file name from a media title: no path separators or control characters.
QString safeFileName(QString name)
{
    static const QRegularExpression unsafe(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]"));
    name.replace(unsafe, QStringLiteral("_"));
    name = name.simplified();
    if (name.size() > 120)
        name.truncate(120);
    return name;
}

} // namespace

MediaCutterDialog::MediaCutterDialog(const Setup &setup, QWidget *parent)
    : QDialog(parent)
    , m_setup(setup)
    , m_cutter(new MediaCutter(this))
    , m_start(new QLineEdit(this))
    , m_end(new QLineEdit(this))
    , m_duration(new QLabel(this))
    , m_copy(new QRadioButton(tr("Lossless Stream Copy (Instant, no re-encode)"), this))
    , m_audio(new QRadioButton(tr("Extract Audio Only"), this))
    , m_audioFormat(new QComboBox(this))
    , m_output(new QLineEdit(this))
    , m_status(new QLabel(this))
    , m_exportButton(new QPushButton(tr("Export"), this))
{
    setObjectName(QStringLiteral("MediaCutterDialog"));
    setWindowTitle(tr("Cut / Extract Media"));
    setMinimumWidth(560);

    // HH:MM:SS.zzz, also accepting MM:SS and plain seconds.
    auto *validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^\\s*(\\d+:)?(\\d{0,2}:)?\\d*([.,]\\d{0,3})?\\s*$")), this);
    auto timeRow = [this, validator](QLineEdit *edit, const char *name, const char *buttonName) {
        edit->setObjectName(QString::fromLatin1(name));
        edit->setValidator(validator);
        edit->setPlaceholderText(QStringLiteral("HH:MM:SS.zzz"));
        auto *now = new QPushButton(tr("Use Current Time"), this);
        now->setObjectName(QString::fromLatin1(buttonName));
        now->setAutoDefault(false);
        now->setEnabled(bool(m_setup.currentTime));
        connect(now, &QPushButton::clicked, this, [this, edit] {
            edit->setText(MediaCutter::formatTimestamp(m_setup.currentTime()));
        });
        auto *row = new QHBoxLayout;
        row->addWidget(edit, 1);
        row->addWidget(now);
        return row;
    };
    m_start->setText(MediaCutter::formatTimestamp(setup.start));
    m_end->setText(MediaCutter::formatTimestamp(setup.end));
    m_duration->setObjectName(QStringLiteral("CutterDuration"));

    m_copy->setObjectName(QStringLiteral("CutterCopyMode"));
    m_audio->setObjectName(QStringLiteral("CutterAudioMode"));
    m_copy->setChecked(true);
    m_copy->setToolTip(tr("Copies the streams as they are, so it is instant and loses nothing. The picture "
                          "starts at the first keyframe after the In-point."));
    auto *modes = new QButtonGroup(this);
    modes->addButton(m_copy);
    modes->addButton(m_audio);
    m_audioFormat->setObjectName(QStringLiteral("CutterAudioFormat"));
    m_audioFormat->addItem(QStringLiteral("MP3 (.mp3)"), int(MediaCutter::AudioFormat::Mp3));
    m_audioFormat->addItem(QStringLiteral("AAC (.aac)"), int(MediaCutter::AudioFormat::Aac));
    m_audioFormat->addItem(QStringLiteral("FLAC (.flac, lossless)"), int(MediaCutter::AudioFormat::Flac));
    auto *audioRow = new QHBoxLayout;
    audioRow->addWidget(m_audio);
    audioRow->addWidget(m_audioFormat);
    audioRow->addStretch();

    m_output->setObjectName(QStringLiteral("CutterOutput"));
    m_output->setText(defaultOutput(setup.input, setup.title, outputSuffix()));
    auto *browse = new QPushButton(tr("Browse..."), this);
    browse->setAutoDefault(false);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_output, 1);
    outputRow->addWidget(browse);

    auto *form = new QFormLayout;
    form->addRow(tr("Start (In):"), timeRow(m_start, "CutterStart", "CutterStartNow"));
    form->addRow(tr("End (Out):"), timeRow(m_end, "CutterEnd", "CutterEndNow"));
    form->addRow(tr("Duration:"), m_duration);
    form->addRow(tr("Export:"), m_copy);
    form->addRow(QString(), audioRow);
    form->addRow(tr("Save as:"), outputRow);

    m_status->setObjectName(QStringLiteral("CutterStatus"));
    m_status->setWordWrap(true);
    m_exportButton->setObjectName(QStringLiteral("CutterExportButton"));
    m_exportButton->setDefault(true);
    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(m_exportButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(QFileInfo(setup.input).fileName().toHtmlEscaped()), this));
    layout->addLayout(form);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    connect(m_start, &QLineEdit::textChanged, this, &MediaCutterDialog::updateState);
    connect(m_end, &QLineEdit::textChanged, this, &MediaCutterDialog::updateState);
    connect(m_output, &QLineEdit::textEdited, this, [this] { m_outputEdited = true; });
    connect(m_output, &QLineEdit::textChanged, this, &MediaCutterDialog::updateState);
    connect(m_copy, &QRadioButton::toggled, this, &MediaCutterDialog::updateOutputSuffix);
    connect(m_audioFormat, &QComboBox::currentIndexChanged, this, [this] {
        m_audio->setChecked(true);
        updateOutputSuffix();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString suffix = outputSuffix();
        const QString file = QFileDialog::getSaveFileName(this, tr("Save Clip As"), m_output->text(),
                                                          tr("%1 files (*.%2);;All Files (*)").arg(suffix.toUpper(), suffix));
        if (!file.isEmpty()) {
            m_output->setText(file);
            m_outputEdited = true;
        }
    });
    connect(m_exportButton, &QPushButton::clicked, this, &MediaCutterDialog::startExport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_cutter, &MediaCutter::progress, this, [this](double fraction) {
        if (m_progress)
            m_progress->setValue(qRound(fraction * 1000));
    });
    connect(m_cutter, &MediaCutter::finished, this, [this](bool ok, const QString &path, const QString &error) {
        if (m_progress) {
            m_progress->disconnect(this);
            m_progress->deleteLater();
            m_progress = nullptr;
        }
        m_exportButton->setEnabled(true);
        if (!ok) {
            m_status->setText(tr("Export failed: %1").arg(error));
            QPalette palette = m_status->palette();
            palette.setColor(QPalette::WindowText, kErrorColor);
            m_status->setPalette(palette);
            return;
        }
        Q_EMIT exported(path);
        accept();
    });

    if (MediaCutter::ffmpegPath().isEmpty()) {
        m_status->setText(tr("ffmpeg is needed to cut media. Install it with <b>sudo dnf install ffmpeg</b> "
                             "(Fedora, from RPM Fusion) or <b>sudo apt install ffmpeg</b> (Ubuntu, Debian)."));
        m_status->setTextFormat(Qt::RichText);
    }
    updateState();
}

QString MediaCutterDialog::defaultOutput(const QString &input, const QString &title, const QString &suffix)
{
    const QFileInfo info(input);
    QString base = safeFileName(title);
    if (base.isEmpty() || base == info.fileName())
        base = info.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("clip");
    QString dir = info.absolutePath();
    if (!QFileInfo(dir).isWritable()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
    }
    const QString stem = QDir(dir).filePath(base + QStringLiteral("_clip"));
    QString path = stem + QLatin1Char('.') + suffix;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QStringLiteral("%1%2.%3").arg(stem).arg(n).arg(suffix);
    return path;
}

QString MediaCutterDialog::outputSuffix() const
{
    if (m_audio->isChecked())
        return MediaCutter::audioSuffix(MediaCutter::AudioFormat(m_audioFormat->currentData().toInt()));
    const QString suffix = QFileInfo(m_setup.input).suffix().toLower();
    return suffix.isEmpty() ? QStringLiteral("mkv") : suffix;
}

void MediaCutterDialog::updateOutputSuffix()
{
    if (!m_outputEdited) {
        m_output->setText(defaultOutput(m_setup.input, m_setup.title, outputSuffix()));
        return;
    }
    // Keep the user's name, with the extension the mode needs.
    const QFileInfo info(m_output->text());
    m_output->setText(info.dir().filePath(info.completeBaseName() + QLatin1Char('.') + outputSuffix()));
}

MediaCutter::Job MediaCutterDialog::job() const
{
    MediaCutter::Job job;
    job.input = m_setup.input;
    job.output = QFileInfo(m_output->text().trimmed()).absoluteFilePath();
    job.start = MediaCutter::parseTimestamp(m_start->text());
    job.end = MediaCutter::parseTimestamp(m_end->text());
    job.mode = m_audio->isChecked() ? MediaCutter::Mode::AudioOnly : MediaCutter::Mode::StreamCopy;
    job.audioFormat = MediaCutter::AudioFormat(m_audioFormat->currentData().toInt());
    return job;
}

QString MediaCutterDialog::validationError() const
{
    const MediaCutter::Job current = job();
    if (MediaCutter::ffmpegPath().isEmpty())
        return tr("ffmpeg is not installed.");
    if (current.start < 0)
        return tr("The start time is not a valid time.");
    if (current.end < 0)
        return tr("The end time is not a valid time.");
    if (current.end <= current.start)
        return tr("The end must be after the start.");
    if (m_setup.duration > 0 && current.start >= m_setup.duration)
        return tr("The start is past the end of the file.");
    if (m_output->text().trimmed().isEmpty())
        return tr("Choose where to save the clip.");
    if (QFileInfo(current.output) == QFileInfo(current.input))
        return tr("The clip can't replace the file it is cut from.");
    if (!QFileInfo(QFileInfo(current.output).absolutePath()).isWritable())
        return tr("The folder %1 is not writable.").arg(QFileInfo(current.output).absolutePath());
    return {};
}

void MediaCutterDialog::updateState()
{
    const MediaCutter::Job current = job();
    if (current.start >= 0 && current.end > current.start) {
        double end = current.end;
        if (m_setup.duration > 0)
            end = std::min(end, m_setup.duration);
        m_duration->setText(MediaCutter::formatTimestamp(end - current.start));
    } else {
        m_duration->setText(QStringLiteral("–"));
    }
    const QString error = validationError();
    m_exportButton->setEnabled(error.isEmpty() && !m_cutter->isRunning());
    if (!MediaCutter::ffmpegPath().isEmpty()) {
        m_status->setText(error);
        QPalette palette = m_status->palette();
        palette.setColor(QPalette::WindowText, kErrorColor);
        m_status->setPalette(palette);
    }
}

void MediaCutterDialog::startExport()
{
    if (!validationError().isEmpty() || m_cutter->isRunning())
        return;
    MediaCutter::Job current = job();
    if (m_setup.duration > 0)
        current.end = std::min(current.end, m_setup.duration);
    if (QFileInfo::exists(current.output)
        && QMessageBox::question(this, tr("Replace File?"),
                                 tr("%1 already exists. Replace it?").arg(QFileInfo(current.output).fileName()))
            != QMessageBox::Yes) {
        return;
    }
    if (!m_cutter->start(current)) {
        m_status->setText(tr("Could not start ffmpeg."));
        return;
    }
    m_exportButton->setEnabled(false);
    m_status->clear();
    m_progress = new QProgressDialog(tr("Cutting %1...").arg(QFileInfo(current.output).fileName()), tr("Cancel"), 0, 1000, this);
    m_progress->setObjectName(QStringLiteral("CutterProgress"));
    m_progress->setWindowTitle(tr("Exporting Clip"));
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    m_progress->setValue(0);
    connect(m_progress, &QProgressDialog::canceled, this, [this] {
        m_cutter->cancel();
        if (m_progress) {
            m_progress->deleteLater();
            m_progress = nullptr;
        }
        m_exportButton->setEnabled(true);
        m_status->setText(tr("Export cancelled."));
    });
}
