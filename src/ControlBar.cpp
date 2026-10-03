#include "ControlBar.h"
#include "Icons.h"
#include "MpvWidget.h"
#include "SeekBar.h"
#include "Theme.h"
#include "TimeFormat.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

ControlBar::ControlBar(MpvWidget *mpv, QWidget *parent)
    : QFrame(parent)
    , m_mpv(mpv)
    , m_seekBar(new SeekBar(this))
{
    setObjectName(QStringLiteral("ControlBar"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 4);
    layout->setSpacing(0);
    layout->addWidget(m_seekBar);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    layout->addLayout(buttons);

    QToolButton *open = addButton(QStringLiteral("OpenButton"), tr("Open File (Ctrl+O)"), IconType::Open);
    QToolButton *previous = addButton(QStringLiteral("PreviousButton"), tr("Previous (PgUp)"), IconType::Previous);
    m_playButton = addButton(QStringLiteral("PlayButton"), tr("Play / Pause (Space)"), IconType::Play);
    QToolButton *stop = addButton(QStringLiteral("StopButton"), tr("Stop"), IconType::Stop);
    QToolButton *next = addButton(QStringLiteral("NextButton"), tr("Next (PgDn)"), IconType::Next);
    for (QToolButton *button : {open, previous, m_playButton, stop, next})
        buttons->addWidget(button);

    m_timeLabel = new QLabel(this);
    m_timeLabel->setObjectName(QStringLiteral("TimeLabel"));
    buttons->addSpacing(8);
    buttons->addWidget(m_timeLabel);
    buttons->addStretch();

    m_muteButton = addButton(QStringLiteral("MuteButton"), tr("Mute (M)"), IconType::Volume);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setObjectName(QStringLiteral("VolumeSlider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setFixedWidth(90);
    m_volumeSlider->setFocusPolicy(Qt::NoFocus);
    m_volumeSlider->setToolTip(tr("Volume"));
    m_playlistButton = addButton(QStringLiteral("PlaylistButton"), tr("Playlist (F6)"), IconType::Playlist);
    m_playlistButton->setCheckable(true);
    QToolButton *fullScreen = addButton(QStringLiteral("FullScreenButton"), tr("Fullscreen (Enter)"), IconType::Fullscreen);
    buttons->addWidget(m_muteButton);
    buttons->addWidget(m_volumeSlider);
    buttons->addSpacing(6);
    buttons->addWidget(m_playlistButton);
    buttons->addWidget(fullScreen);

    connect(open, &QToolButton::clicked, this, &ControlBar::openRequested);
    connect(previous, &QToolButton::clicked, m_mpv, &MpvWidget::playlistPrev);
    connect(next, &QToolButton::clicked, m_mpv, &MpvWidget::playlistNext);
    connect(stop, &QToolButton::clicked, m_mpv, &MpvWidget::stop);
    connect(m_playButton, &QToolButton::clicked, m_mpv, &MpvWidget::togglePause);
    connect(m_muteButton, &QToolButton::clicked, this,
            [this] { m_mpv->command({QStringLiteral("cycle"), QStringLiteral("mute")}); });
    connect(m_volumeSlider, &QSlider::valueChanged, this,
            [this](int value) { m_mpv->setMpvProperty(QStringLiteral("volume"), QString::number(value)); });
    connect(m_playlistButton, &QToolButton::toggled, this, &ControlBar::playlistToggled);
    connect(fullScreen, &QToolButton::clicked, this, &ControlBar::fullScreenRequested);

    connect(m_seekBar, &SeekBar::seekRequested, this, [this](double seconds, bool exact) {
        m_mpv->command({QStringLiteral("seek"), QString::number(seconds, 'f', 3),
                        exact ? QStringLiteral("absolute+exact") : QStringLiteral("absolute+keyframes")});
    });
    connect(m_mpv, &MpvWidget::propertyUpdated, this, &ControlBar::onPropertyUpdated);

    // Volume and mute already have values; mirror them now.
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setMaximum(m_mpv->mpvProperty(QStringLiteral("volume-max")).toInt());
    m_volumeSlider->setValue(m_mpv->mpvProperty(QStringLiteral("volume")).toInt());
    updateTimeLabel();
}

void ControlBar::setPlaylistChecked(bool checked)
{
    const QSignalBlocker blocker(m_playlistButton);
    m_playlistButton->setChecked(checked);
}

QToolButton *ControlBar::addButton(const QString &objectName, const QString &toolTip, IconType icon)
{
    auto *button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setIcon(skinIcon(icon));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void ControlBar::onPropertyUpdated(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("time-pos")) {
        m_position = value.toDouble();
        m_seekBar->setPosition(m_position);
        updateTimeLabel();
    } else if (name == QLatin1String("duration")) {
        m_duration = value.toDouble();
        m_seekBar->setDuration(m_duration);
        updateTimeLabel();
    } else if (name == QLatin1String("pause")) {
        m_paused = value.toBool();
        updatePlayButton();
    } else if (name == QLatin1String("idle-active")) {
        m_idle = value.toBool();
        updatePlayButton();
        if (m_idle) {
            // Stopped: rewind the seekbar instead of keeping the last position.
            m_position = 0;
            m_duration = 0;
            m_seekBar->setDuration(0);
            m_seekBar->setPosition(0);
            m_seekBar->setChapters({});
            updateTimeLabel();
        }
    } else if (name == QLatin1String("mute")) {
        m_muteButton->setIcon(skinIcon(value.toBool() ? IconType::Muted : IconType::Volume));
    } else if (name == QLatin1String("volume")) {
        const QSignalBlocker blocker(m_volumeSlider);
        m_volumeSlider->setValue(qRound(value.toDouble()));
    } else if (name == QLatin1String("chapter-list")) {
        QList<double> chapters;
        for (const QVariant &chapter : value.toList())
            chapters.append(chapter.toMap().value(QStringLiteral("time")).toDouble());
        m_seekBar->setChapters(chapters);
    }
}

void ControlBar::updatePlayButton()
{
    // Nothing plays while idle, even though mpv's pause flag is off.
    const bool playing = !m_paused && !m_idle;
    m_playButton->setIcon(skinIcon(playing ? IconType::Pause : IconType::Play));
}

void ControlBar::updateTimeLabel()
{
    m_timeLabel->setText(QStringLiteral("<span style='color:%1'>%2</span>"
                                        "<span style='color:%3'> / %4</span>")
                             .arg(Theme::hex(Theme::Accent), formatTime(m_position),
                                  Theme::hex(Theme::TextSecondary), formatTime(m_duration)));
}
