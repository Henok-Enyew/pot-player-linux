#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <array>

class MpvWidget;

// A 10-band graphic equalizer, applied as mpv's "@eq" audio filter (a chain
// of ffmpeg equalizer filters, one per band that is not at 0 dB). The gains
// are saved and restored on the next start.
class Equalizer : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBandCount = 10;
    static constexpr double kMaxGain = 12;
    using Gains = std::array<double, kBandCount>;

    struct Preset {
        QString id;   // stable key, saved in the settings
        QString name; // translated
        Gains gains;
    };

    // Center frequencies in Hz: 31 Hz to 16 kHz, an octave apart.
    static constexpr std::array<int, kBandCount> kFrequencies{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    // "31", "1k", ...
    static QString bandLabel(int band);
    static QList<Preset> presets();
    // The lavfi graph for `gains`, e.g. "lavfi=[equalizer=f=31:t=o:w=1:g=6]";
    // empty when every band is at 0 dB.
    static QString filter(const Gains &gains);

    Equalizer(MpvWidget *mpv, QObject *parent = nullptr);

    Gains gains() const { return m_gains; }
    double gain(int band) const { return m_gains[band]; }
    // The preset matching the current gains, or "custom".
    QString presetId() const;
    QString presetName() const;

    void setPreset(const QString &id);
    // Clamped to +-kMaxGain. While a slider is dragged the filter is updated
    // at most every few dozen milliseconds.
    void setGain(int band, double gain);

Q_SIGNALS:
    void changed();

private:
    void setGains(const Gains &gains, bool immediately);
    void apply();
    void save() const;

    MpvWidget *m_mpv;
    Gains m_gains{};
    bool m_applied = false;
    QTimer m_applyTimer;
};
