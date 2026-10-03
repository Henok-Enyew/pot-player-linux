#pragma once

#include <QFrame>

enum class IconType;
class MpvWidget;
class QLabel;
class QSlider;
class QToolButton;
class SeekBar;

// Bottom control strip: seekbar on top, transport and volume buttons below.
class ControlBar : public QFrame
{
    Q_OBJECT

public:
    enum class Repeat { Off, All, One };
    // Fit: scaled to the window at the video's aspect; Wide: stretched to
    // 16:9; Original: 100%, one video pixel per screen pixel.
    enum class Aspect { Fit, Wide, Original };

    ControlBar(MpvWidget *mpv, QWidget *parent = nullptr);

    SeekBar *seekBar() const { return m_seekBar; }
    void setPlaylistChecked(bool checked);

    // Mirrors of mpv's shuffle, loop-file/loop-playlist and
    // video-unscaled/video-aspect-override, however they were set.
    bool isShuffle() const { return m_shuffle; }
    Repeat repeat() const { return m_repeat; }
    Aspect aspect() const { return m_aspect; }
    // What the buttons do: shuffle on/off, Off -> All -> One -> Off, Fit -> 16:9 -> 100% -> Fit.
    void setShuffle(bool on);
    void setRepeat(Repeat mode);
    void setAspect(Aspect mode);

Q_SIGNALS:
    void openRequested();
    void playlistToggled(bool visible);
    void fullScreenRequested();
    // Feedback for the OSD, e.g. ("Repeat", "All").
    void message(const QString &label, const QString &value);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QToolButton *addButton(const QString &objectName, const QString &toolTip, IconType icon);
    void onPropertyUpdated(const QString &name, const QVariant &value);
    void updatePlayButton();
    void updateTimeLabel();
    void updateModeButtons();

    MpvWidget *m_mpv;
    SeekBar *m_seekBar;
    QToolButton *m_playButton;
    QToolButton *m_muteButton;
    QToolButton *m_playlistButton;
    QToolButton *m_shuffleButton;
    QToolButton *m_repeatButton;
    QToolButton *m_aspectButton;
    QSlider *m_volumeSlider;
    QLabel *m_timeLabel;
    double m_position = 0;
    double m_duration = 0;
    bool m_paused = false;
    bool m_idle = true;
    bool m_shuffle = false;
    bool m_loopFile = false;
    bool m_loopPlaylist = false;
    bool m_unscaled = false;
    double m_aspectOverride = -1;
    Repeat m_repeat = Repeat::Off;
    Aspect m_aspect = Aspect::Fit;
};
