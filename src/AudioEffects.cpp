#include "AudioEffects.h"
#include "MpvWidget.h"
#include "PlaylistSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kFormatVersion = 1;
// Coalesces slider movements into one filter rebuild.
constexpr int kApplyDelayMs = 30;

QString number(double value)
{
    return QString::number(std::round(value * 10) / 10, 'g', 4);
}

bool isZero(double value)
{
    return std::abs(value) < 0.05;
}

} // namespace

namespace AudioEffects {

QList<Preset> presets()
{
    return {
        {QObject::tr("Flat"), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {QObject::tr("Bass Boost"), {7, 6, 5, 3, 1, 0, 0, 0, 0, 0}},
        {QObject::tr("Club"), {0, 0, 4, 3, 3, 3, 2, 0, 0, 0}},
        {QObject::tr("Rock"), {4, 3, -2, -3, -1, 1, 3, 4, 4, 4}},
        {QObject::tr("Vocal Clear"), {-2, -2, -1, 0, 2, 4, 4, 3, 1, 0}},
        {QObject::tr("Cinema/Action"), {5, 4, 2, 0, -1, 0, 2, 3, 3, 2}},
    };
}

QString matchingPreset(const Settings &settings)
{
    for (const Preset &preset : presets()) {
        if (preset.bands == settings.bands && preset.bass == settings.bass && preset.treble == settings.treble)
            return preset.name;
    }
    return {};
}

QString filterLabel()
{
    return QStringLiteral("potfx");
}

QString filterChain(const Settings &settings)
{
    QStringList filters;
    if (!isZero(settings.preamp))
        filters.append(QStringLiteral("volume=%1dB").arg(number(settings.preamp)));
    if (!isZero(settings.bass))
        filters.append(QStringLiteral("bass=g=%1:f=110:w=0.6").arg(number(settings.bass)));
    if (!isZero(settings.treble))
        filters.append(QStringLiteral("treble=g=%1:f=3000:w=0.6").arg(number(settings.treble)));
    for (int i = 0; i < kBandCount; ++i) {
        if (!isZero(settings.bands[i])) {
            filters.append(QStringLiteral("equalizer=f=%1:width_type=o:w=1:g=%2")
                               .arg(kBandFrequencies[i])
                               .arg(number(settings.bands[i])));
        }
    }
    if (settings.normalize)
        filters.append(QStringLiteral("dynaudnorm=f=200:g=15:m=10"));
    if (filters.isEmpty())
        return {};
    // The brackets quote the graph, so its commas don't split mpv's filter list.
    return QStringLiteral("@%1:lavfi=[%2]").arg(filterLabel(), filters.join(QLatin1Char(',')));
}

QString settingsFile()
{
    return PlaylistSession::configDir() + QStringLiteral("/audio_settings.json");
}

Settings load(const QString &path)
{
    Settings settings;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return settings;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion)
        return settings;
    settings.preamp = std::clamp(root.value(QStringLiteral("preamp")).toDouble(), kPreampMin, kPreampMax);
    settings.bass = std::clamp(root.value(QStringLiteral("bass")).toDouble(), kToneMin, kToneMax);
    settings.treble = std::clamp(root.value(QStringLiteral("treble")).toDouble(), kToneMin, kToneMax);
    const QJsonArray bands = root.value(QStringLiteral("bands")).toArray();
    for (int i = 0; i < kBandCount && i < bands.size(); ++i)
        settings.bands[i] = std::clamp(bands[i].toDouble(), kBandMin, kBandMax);
    settings.normalize = root.value(QStringLiteral("normalize")).toBool();
    return settings;
}

bool save(const Settings &settings, const QString &path)
{
    QJsonArray bands;
    for (double band : settings.bands)
        bands.append(band);
    const QJsonObject root{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("preamp"), settings.preamp},
        {QStringLiteral("bass"), settings.bass},
        {QStringLiteral("treble"), settings.treble},
        {QStringLiteral("bands"), bands},
        {QStringLiteral("normalize"), settings.normalize},
    };
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace AudioEffects

AudioEffectsController::AudioEffectsController(MpvWidget *mpv, QObject *parent)
    : QObject(parent)
    , m_mpv(mpv)
    , m_settings(AudioEffects::load())
{
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(kApplyDelayMs);
    connect(&m_applyTimer, &QTimer::timeout, this, &AudioEffectsController::commit);
    apply();
}

void AudioEffectsController::setSettings(const AudioEffects::Settings &settings)
{
    if (settings == m_settings)
        return;
    m_settings = settings;
    m_applyTimer.start();
    Q_EMIT settingsChanged(m_settings);
}

void AudioEffectsController::flush()
{
    if (m_applyTimer.isActive()) {
        m_applyTimer.stop();
        commit();
    }
}

void AudioEffectsController::commit()
{
    apply();
    AudioEffects::save(m_settings);
}

void AudioEffectsController::apply()
{
    const QString chain = AudioEffects::filterChain(m_settings);
    if (chain != m_applied) {
        m_applied = chain;
        m_mpv->setMpvProperty(QStringLiteral("af"), chain);
    }
}
