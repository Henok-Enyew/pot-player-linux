#include "ControlBar.h"
#include "Icons.h"
#include "MpvWidget.h"
#include "SeekBar.h"
#include "Theme.h"
#include "TimeFormat.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr double kWideAspect = 16.0 / 9.0;
// Below this width the mode buttons make room for the essentials.
constexpr int kCompactWidth = 560;

// loop-file, loop-playlist and video-unscaled read as a flag, a count or a
// string ("inf", "downscale-big") depending on their value.
bool isOn(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::QString: {
        const QString text = value.toString();
        return !text.isEmpty() && text != QLatin1String("no") && text != QLatin1String("false");
    }
    case QMetaType::LongLong:
    case QMetaType::Int:
    case QMetaType::Double:
        return value.toDouble() > 0;
    default:
        return false;
    }
}

} // namespace

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
    m_shuffleButton = addButton(QStringLiteral("ShuffleButton"), tr("Shuffle"), IconType::Shuffle);
    m_shuffleButton->setCheckable(true);
    m_repeatButton = addButton(QStringLiteral("RepeatButton"), QString(), IconType::Repeat);
    m_repeatButton->setCheckable(true);
    for (QToolButton *button : {open, previous, m_playButton, stop, next})
        buttons->addWidget(button);
    buttons->addSpacing(4);
    buttons->addWidget(m_shuffleButton);
    buttons->addWidget(m_repeatButton);

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
    m_aspectButton = addButton(QStringLiteral("AspectButton"), QString(), IconType::AspectFit);
    QToolButton *fullScreen = addButton(QStringLiteral("FullScreenButton"), tr("Fullscreen (Enter)"), IconType::Fullscreen);
    buttons->addWidget(m_muteButton);
    buttons->addWidget(m_volumeSlider);
    buttons->addSpacing(6);
    buttons->addWidget(m_aspectButton);
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
    // The buttons only ask mpv; their look follows mpv's properties.
    connect(m_shuffleButton, &QToolButton::clicked, this, [this] {
        setShuffle(!m_shuffle);
        updateModeButtons();
    });
    connect(m_repeatButton, &QToolButton::clicked, this, [this] {
        setRepeat(m_repeat == Repeat::Off ? Repeat::All : m_repeat == Repeat::All ? Repeat::One : Repeat::Off);
        updateModeButtons();
    });
    connect(m_aspectButton, &QToolButton::clicked, this, [this] {
        setAspect(m_aspect == Aspect::Fit ? Aspect::Wide : m_aspect == Aspect::Wide ? Aspect::Original : Aspect::Fit);
    });

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
    updateModeButtons();
}

void ControlBar::setShuffle(bool on)
{
    // The option is the mode the button shows; changing it doesn't reorder
    // the playlist by itself. playlist-unshuffle restores the order from
    // before playlist-shuffle, keeping entries added since.
    m_mpv->setMpvProperty(QStringLiteral("shuffle"), on ? QStringLiteral("yes") : QStringLiteral("no"));
    m_mpv->command({on ? QStringLiteral("playlist-shuffle") : QStringLiteral("playlist-unshuffle")});
    Q_EMIT message(tr("Shuffle"), on ? tr("On") : tr("Off"));
}

void ControlBar::setRepeat(Repeat mode)
{
    m_mpv->setMpvProperty(QStringLiteral("loop-file"), mode == Repeat::One ? QStringLiteral("inf") : QStringLiteral("no"));
    m_mpv->setMpvProperty(QStringLiteral("loop-playlist"), mode == Repeat::All ? QStringLiteral("inf") : QStringLiteral("no"));
    Q_EMIT message(tr("Repeat"), mode == Repeat::One ? tr("One") : mode == Repeat::All ? tr("All") : tr("Off"));
}

void ControlBar::setAspect(Aspect mode)
{
    m_mpv->setMpvProperty(QStringLiteral("video-unscaled"), mode == Aspect::Original ? QStringLiteral("yes") : QStringLiteral("no"));
    m_mpv->setMpvProperty(QStringLiteral("video-aspect-override"), mode == Aspect::Wide ? QStringLiteral("16:9") : QStringLiteral("-1"));
    Q_EMIT message(tr("Aspect"), mode == Aspect::Original ? tr("100%") : mode == Aspect::Wide ? QStringLiteral("16:9") : tr("Fit to Window"));
}

void ControlBar::updateModeButtons()
{
    m_repeat = m_loopFile ? Repeat::One : m_loopPlaylist ? Repeat::All : Repeat::Off;
    m_aspect = m_unscaled ? Aspect::Original
        : std::abs(m_aspectOverride - kWideAspect) < 0.01 ? Aspect::Wide : Aspect::Fit;

    const QSignalBlocker shuffleBlocker(m_shuffleButton);
    m_shuffleButton->setChecked(m_shuffle);
    m_shuffleButton->setToolTip(m_shuffle ? tr("Shuffle: On") : tr("Shuffle: Off"));

    const QSignalBlocker repeatBlocker(m_repeatButton);
    m_repeatButton->setChecked(m_repeat != Repeat::Off);
    m_repeatButton->setIcon(skinIcon(m_repeat == Repeat::One ? IconType::RepeatOne : IconType::Repeat));
    switch (m_repeat) {
    case Repeat::Off:
        m_repeatButton->setToolTip(tr("Repeat: Off"));
        break;
    case Repeat::All:
        m_repeatButton->setToolTip(tr("Repeat: All"));
        break;
    case Repeat::One:
        m_repeatButton->setToolTip(tr("Repeat: One"));
        break;
    }

    switch (m_aspect) {
    case Aspect::Fit:
        m_aspectButton->setIcon(skinIcon(IconType::AspectFit));
        m_aspectButton->setToolTip(tr("Aspect: Fit to Window"));
        break;
    case Aspect::Wide:
        m_aspectButton->setIcon(skinIcon(IconType::AspectWide));
        m_aspectButton->setToolTip(tr("Aspect: 16:9"));
        break;
    case Aspect::Original:
        m_aspectButton->setIcon(skinIcon(IconType::AspectOriginal));
        m_aspectButton->setToolTip(tr("Aspect: 100%"));
        break;
    }
}

void ControlBar::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    // Narrow windows keep the transport, time, volume, playlist and fullscreen buttons.
    const bool roomy = event->size().width() >= kCompactWidth;
    for (QToolButton *button : {m_shuffleButton, m_repeatButton, m_aspectButton})
        button->setVisible(roomy);
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
    } else if (name == QLatin1String("shuffle")) {
        m_shuffle = isOn(value);
        updateModeButtons();
    } else if (name == QLatin1String("loop-file")) {
        m_loopFile = isOn(value);
        updateModeButtons();
    } else if (name == QLatin1String("loop-playlist")) {
        m_loopPlaylist = isOn(value);
        updateModeButtons();
    } else if (name == QLatin1String("video-unscaled")) {
        m_unscaled = isOn(value);
        updateModeButtons();
    } else if (name == QLatin1String("video-aspect-override")) {
        m_aspectOverride = value.isValid() ? value.toDouble() : -1;
        updateModeButtons();
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
    // Rich text is laid out again on every change: only when a second ticks over.
    const QString text = QStringLiteral("<span style='color:%1'>%2</span>"
                                        "<span style='color:%3'> / %4</span>")
                             .arg(Theme::hex(Theme::Accent), formatTime(m_position),
                                  Theme::hex(Theme::TextSecondary), formatTime(m_duration));
    if (text != m_timeLabel->text())
        m_timeLabel->setText(text);
}
