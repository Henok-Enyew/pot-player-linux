#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <array>

class MpvWidget;

// Preamp, bass and treble, a 10-band graphic equalizer and loudness
// normalization, applied through mpv's "af" property as one lavfi filter
// graph, and kept in ~/.config/potplayer-linux/audio_settings.json.
namespace AudioEffects {

constexpr int kBandCount = 10;
// Centre frequencies of the bands, in Hz.
constexpr std::array<int, kBandCount> kBandFrequencies{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

constexpr double kPreampMin = -10, kPreampMax = 10;
constexpr double kToneMin = -15, kToneMax = 20; // bass and treble
constexpr double kBandMin = -12, kBandMax = 12;

struct Settings {
    double preamp = 0; // dB
    double bass = 0;   // dB at 110 Hz
    double treble = 0; // dB at 3 kHz
    std::array<double, kBandCount> bands{};
    bool normalize = false; // night mode
    bool operator==(const Settings &other) const
    {
        return preamp == other.preamp && bass == other.bass && treble == other.treble && bands == other.bands
            && normalize == other.normalize;
    }
    bool operator!=(const Settings &other) const { return !(*this == other); }
};

struct Preset {
    QString name;
    std::array<double, kBandCount> bands;
    double bass = 0;
    double treble = 0;
};

// Flat, Bass Boost, Club, Rock, Vocal Clear, Cinema/Action.
QList<Preset> presets();
// The preset whose values `settings` has, or an empty string ("Custom").
QString matchingPreset(const Settings &settings);

// The value for mpv's "af" property; empty when every effect is off.
QString filterChain(const Settings &settings);
// The label of the filter in mpv's filter chain.
QString filterLabel();

QString settingsFile();
Settings load(const QString &path = settingsFile());
bool save(const Settings &settings, const QString &path = settingsFile());

} // namespace AudioEffects

// Applies the audio effects to mpv as they change and saves them.
// Changes made within a few milliseconds of each other, as while dragging a
// slider, rebuild the filter graph once.
class AudioEffectsController : public QObject
{
    Q_OBJECT

public:
    // Loads the saved settings and applies them.
    explicit AudioEffectsController(MpvWidget *mpv, QObject *parent = nullptr);

    const AudioEffects::Settings &settings() const { return m_settings; }
    void setSettings(const AudioEffects::Settings &settings);
    // Applies pending changes now instead of after the short delay.
    void flush();

Q_SIGNALS:
    void settingsChanged(const AudioEffects::Settings &settings);

private:
    // Sets mpv's filter chain to match the settings.
    void apply();
    // Applies the settings and saves them.
    void commit();

    MpvWidget *m_mpv;
    AudioEffects::Settings m_settings;
    QString m_applied;
    QTimer m_applyTimer;
};
