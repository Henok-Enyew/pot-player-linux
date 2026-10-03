#pragma once

#include <QDialog>
#include <QList>

class Equalizer;
class QComboBox;
class QLabel;
class QSlider;

// Audio -> Equalizer -> Custom...: a slider per band and a preset picker,
// applied live.
class EqualizerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EqualizerDialog(Equalizer *equalizer, QWidget *parent = nullptr);

private:
    // Shows the equalizer's current gains and preset.
    void sync();

    Equalizer *m_equalizer;
    QComboBox *m_presets;
    QList<QSlider *> m_sliders;
    QList<QLabel *> m_gainLabels;
};
