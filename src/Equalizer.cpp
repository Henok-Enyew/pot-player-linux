#include "Equalizer.h"
#include "MpvWidget.h"
#include "PlaylistSession.h"

#include <QSettings>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kApplyDelayMs = 60;
const QString kLabel = QStringLiteral("@eq");

QString settingsFile()
{
    return PlaylistSession::configDir() + QStringLiteral("/settings.ini");
}

} // namespace

QString Equalizer::bandLabel(int band)
{
    const int hz = kFrequencies[band];
    return hz >= 1000 ? QStringLiteral("%1k").arg(hz / 1000) : QString::number(hz);
}

QList<Equalizer::Preset> Equalizer::presets()
{
    return {
        {QStringLiteral("flat"), tr("Flat"), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("bass"), tr("Bass Boost"), {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("treble"), tr("Treble Boost"), {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
        {QStringLiteral("vocal"), tr("Vocal"), {-2, -2, -1, 1, 3, 4, 3, 1, 0, -1}},
        {QStringLiteral("rock"), tr("Rock"), {5, 4, 2, -1, -2, -1, 1, 3, 4, 5}},
        {QStringLiteral("pop"), tr("Pop"), {-1, 1, 3, 4, 3, 0, -1, -1, 1, 2}},
        {QStringLiteral("jazz"), tr("Jazz"), {3, 2, 1, 2, -1, -1, 0, 1, 2, 3}},
        {QStringLiteral("classical"), tr("Classical"), {4, 3, 2, 1, 0, 0, 0, 2, 3, 4}},
        {QStringLiteral("electronic"), tr("Electronic"), {5, 4, 1, 0, -2, 1, 0, 1, 4, 5}},
    };
}

QString Equalizer::filter(const Gains &gains)
{
    QStringList bands;
    for (int band = 0; band < kBandCount; ++band) {
        if (std::abs(gains[band]) < 0.05)
            continue;
        // One octave wide ("t=o:w=1"), centered on the band.
        bands.append(QStringLiteral("equalizer=f=%1:t=o:w=1:g=%2")
                         .arg(kFrequencies[band])
                         .arg(gains[band], 0, 'f', 1));
    }
    if (bands.isEmpty())
        return {};
    // mpv's brackets keep the graph's ':' and ',' away from its own option parser.
    return QStringLiteral("lavfi=[%1]").arg(bands.join(QLatin1Char(',')));
}

Equalizer::Equalizer(MpvWidget *mpv, QObject *parent)
    : QObject(parent)
    , m_mpv(mpv)
{
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(kApplyDelayMs);
    connect(&m_applyTimer, &QTimer::timeout, this, &Equalizer::apply);

    const QStringList saved = QSettings(settingsFile(), QSettings::IniFormat)
                                  .value(QStringLiteral("equalizer/gains"))
                                  .toStringList();
    if (saved.size() == kBandCount) {
        Gains gains{};
        for (int band = 0; band < kBandCount; ++band)
            gains[band] = std::clamp(saved[band].toDouble(), -kMaxGain, kMaxGain);
        m_gains = gains;
        apply();
    }
}

QString Equalizer::presetId() const
{
    for (const Preset &preset : presets()) {
        if (preset.gains == m_gains)
            return preset.id;
    }
    return QStringLiteral("custom");
}

QString Equalizer::presetName() const
{
    const QString id = presetId();
    for (const Preset &preset : presets()) {
        if (preset.id == id)
            return preset.name;
    }
    return tr("Custom");
}

void Equalizer::setPreset(const QString &id)
{
    for (const Preset &preset : presets()) {
        if (preset.id == id) {
            setGains(preset.gains, true);
            return;
        }
    }
}

void Equalizer::setGain(int band, double gain)
{
    if (band < 0 || band >= kBandCount)
        return;
    Gains gains = m_gains;
    gains[band] = std::clamp(gain, -kMaxGain, kMaxGain);
    setGains(gains, false);
}

void Equalizer::setGains(const Gains &gains, bool immediately)
{
    if (gains == m_gains)
        return;
    m_gains = gains;
    if (immediately) {
        m_applyTimer.stop();
        apply();
    } else if (!m_applyTimer.isActive()) {
        m_applyTimer.start();
    }
    save();
    Q_EMIT changed();
}

void Equalizer::apply()
{
    const QString graph = filter(m_gains);
    if (!graph.isEmpty()) {
        // Adding a filter whose label is in use replaces that filter in place.
        m_mpv->command({QStringLiteral("af"), QStringLiteral("add"), kLabel + QLatin1Char(':') + graph});
        m_applied = true;
    } else if (std::exchange(m_applied, false)) {
        m_mpv->command({QStringLiteral("af"), QStringLiteral("remove"), kLabel});
    }
}

void Equalizer::save() const
{
    QStringList values;
    for (double gain : m_gains)
        values.append(QString::number(gain, 'f', 1));
    QSettings(settingsFile(), QSettings::IniFormat).setValue(QStringLiteral("equalizer/gains"), values);
}
