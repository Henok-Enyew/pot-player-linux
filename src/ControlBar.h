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
    ControlBar(MpvWidget *mpv, QWidget *parent = nullptr);

    SeekBar *seekBar() const { return m_seekBar; }
    void setPlaylistChecked(bool checked);

Q_SIGNALS:
    void openRequested();
    void playlistToggled(bool visible);
    void fullScreenRequested();

private:
    QToolButton *addButton(const QString &objectName, const QString &toolTip, IconType icon);
    void onPropertyUpdated(const QString &name, const QVariant &value);
    void updateTimeLabel();

    MpvWidget *m_mpv;
    SeekBar *m_seekBar;
    QToolButton *m_playButton;
    QToolButton *m_muteButton;
    QToolButton *m_playlistButton;
    QSlider *m_volumeSlider;
    QLabel *m_timeLabel;
    double m_position = 0;
    double m_duration = 0;
};
