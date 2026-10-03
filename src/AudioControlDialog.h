#pragma once

#include "AudioEffects.h"

#include <QDialog>

#include <array>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

// Audio -> Audio Control & Equalizer: preamp, bass and treble, a 10-band
// equalizer with presets, and night mode. Every change is heard at once.
class AudioControlDialog : public QDialog
{
    Q_OBJECT

public:
    AudioControlDialog(AudioEffectsController *effects, QWidget *parent = nullptr);

    // Applies a preset by name; returns false if there is none.
    bool selectPreset(const QString &name);
    void resetToDefault();

private:
    QSlider *makeSlider(Qt::Orientation orientation, double min, double max, const char *name);
    // Shows `settings` on the controls.
    void showSettings(const AudioEffects::Settings &settings);
    // Sends the controls' values to the controller.
    void onControlChanged();

    AudioEffectsController *m_effects;
    QComboBox *m_preset;
    QSlider *m_preamp;
    QSlider *m_bass;
    QSlider *m_treble;
    QLabel *m_preampLabel;
    QLabel *m_bassLabel;
    QLabel *m_trebleLabel;
    std::array<QSlider *, AudioEffects::kBandCount> m_bands{};
    std::array<QLabel *, AudioEffects::kBandCount> m_bandLabels{};
    QCheckBox *m_normalize;
    bool m_updating = false;
};
