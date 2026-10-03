#include "AudioControlDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString gainText(int db)
{
    return db > 0 ? QStringLiteral("+%1 dB").arg(db) : QStringLiteral("%1 dB").arg(db);
}

QString frequencyText(int hz)
{
    return hz >= 1000 ? QStringLiteral("%1k").arg(hz / 1000) : QString::number(hz);
}

} // namespace

AudioControlDialog::AudioControlDialog(AudioEffectsController *effects, QWidget *parent)
    : QDialog(parent)
    , m_effects(effects)
    , m_preset(new QComboBox(this))
    , m_preampLabel(new QLabel(this))
    , m_bassLabel(new QLabel(this))
    , m_trebleLabel(new QLabel(this))
    , m_normalize(new QCheckBox(tr("Loudness Normalization (Night Mode)"), this))
{
    using namespace AudioEffects;
    setObjectName(QStringLiteral("AudioControlDialog"));
    setWindowTitle(tr("Audio Control & Equalizer"));

    m_preset->setObjectName(QStringLiteral("AudioPreset"));
    for (const Preset &preset : presets())
        m_preset->addItem(preset.name);
    m_preset->addItem(tr("Custom"));
    auto *reset = new QPushButton(tr("Reset to Default"), this);
    reset->setObjectName(QStringLiteral("AudioReset"));
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Preset:"), this));
    top->addWidget(m_preset, 1);
    top->addWidget(reset);

    m_preamp = makeSlider(Qt::Horizontal, kPreampMin, kPreampMax, "AudioPreamp");
    m_bass = makeSlider(Qt::Horizontal, kToneMin, kToneMax, "AudioBass");
    m_treble = makeSlider(Qt::Horizontal, kToneMin, kToneMax, "AudioTreble");
    auto *tone = new QGroupBox(tr("Tone"), this);
    auto *toneLayout = new QGridLayout(tone);
    const QList<std::tuple<QString, QSlider *, QLabel *>> toneRows{
        {tr("Preamp"), m_preamp, m_preampLabel},
        {tr("Bass"), m_bass, m_bassLabel},
        {tr("Treble"), m_treble, m_trebleLabel},
    };
    for (int row = 0; row < toneRows.size(); ++row) {
        const auto &[text, slider, label] = toneRows[row];
        label->setMinimumWidth(label->fontMetrics().horizontalAdvance(QStringLiteral("+20 dB")) + 4);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        toneLayout->addWidget(new QLabel(text, tone), row, 0);
        toneLayout->addWidget(slider, row, 1);
        toneLayout->addWidget(label, row, 2);
    }
    toneLayout->setColumnStretch(1, 1);

    auto *equalizer = new QGroupBox(tr("Equalizer"), this);
    auto *bands = new QGridLayout(equalizer);
    for (int i = 0; i < kBandCount; ++i) {
        m_bands[i] = makeSlider(Qt::Vertical, kBandMin, kBandMax, "");
        m_bands[i]->setObjectName(QStringLiteral("AudioBand%1").arg(i));
        m_bands[i]->setMinimumHeight(140);
        m_bands[i]->setToolTip(tr("%1 Hz").arg(kBandFrequencies[i]));
        m_bandLabels[i] = new QLabel(equalizer);
        m_bandLabels[i]->setAlignment(Qt::AlignCenter);
        auto *frequency = new QLabel(frequencyText(kBandFrequencies[i]), equalizer);
        frequency->setAlignment(Qt::AlignCenter);
        bands->addWidget(m_bandLabels[i], 0, i);
        bands->addWidget(m_bands[i], 1, i, Qt::AlignHCenter);
        bands->addWidget(frequency, 2, i);
    }

    m_normalize->setObjectName(QStringLiteral("AudioNightMode"));
    m_normalize->setToolTip(tr("Evens out loud and quiet passages, for listening at low volume"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(tone);
    layout->addWidget(equalizer, 1);
    layout->addWidget(m_normalize);
    layout->addWidget(buttons);

    showSettings(m_effects->settings());

    connect(m_preset, &QComboBox::activated, this, [this] { selectPreset(m_preset->currentText()); });
    connect(reset, &QPushButton::clicked, this, &AudioControlDialog::resetToDefault);
    connect(m_normalize, &QCheckBox::toggled, this, &AudioControlDialog::onControlChanged);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_effects, &AudioEffectsController::settingsChanged, this, [this](const AudioEffects::Settings &settings) {
        if (!m_updating)
            showSettings(settings);
    });
}

QSlider *AudioControlDialog::makeSlider(Qt::Orientation orientation, double min, double max, const char *name)
{
    auto *slider = new QSlider(orientation, this);
    slider->setObjectName(QString::fromLatin1(name));
    slider->setRange(qRound(min), qRound(max));
    slider->setPageStep(3);
    slider->setTickPosition(orientation == Qt::Vertical ? QSlider::TicksBothSides : QSlider::TicksBelow);
    slider->setTickInterval(orientation == Qt::Vertical ? 6 : 5);
    connect(slider, &QSlider::valueChanged, this, &AudioControlDialog::onControlChanged);
    return slider;
}

void AudioControlDialog::showSettings(const AudioEffects::Settings &settings)
{
    m_updating = true;
    m_preamp->setValue(qRound(settings.preamp));
    m_bass->setValue(qRound(settings.bass));
    m_treble->setValue(qRound(settings.treble));
    for (int i = 0; i < AudioEffects::kBandCount; ++i)
        m_bands[i]->setValue(qRound(settings.bands[i]));
    m_normalize->setChecked(settings.normalize);
    m_updating = false;

    m_preampLabel->setText(gainText(m_preamp->value()));
    m_bassLabel->setText(gainText(m_bass->value()));
    m_trebleLabel->setText(gainText(m_treble->value()));
    for (int i = 0; i < AudioEffects::kBandCount; ++i)
        m_bandLabels[i]->setText(QString::number(m_bands[i]->value()));
    const QString preset = AudioEffects::matchingPreset(settings);
    m_preset->setCurrentIndex(preset.isEmpty() ? m_preset->count() - 1 : m_preset->findText(preset));
}

void AudioControlDialog::onControlChanged()
{
    if (m_updating)
        return;
    AudioEffects::Settings settings;
    settings.preamp = m_preamp->value();
    settings.bass = m_bass->value();
    settings.treble = m_treble->value();
    for (int i = 0; i < AudioEffects::kBandCount; ++i)
        settings.bands[i] = m_bands[i]->value();
    settings.normalize = m_normalize->isChecked();
    m_updating = true;
    m_effects->setSettings(settings);
    m_updating = false;
    showSettings(settings);
}

bool AudioControlDialog::selectPreset(const QString &name)
{
    for (const AudioEffects::Preset &preset : AudioEffects::presets()) {
        if (preset.name != name)
            continue;
        // The preamp and night mode are not part of a preset.
        AudioEffects::Settings settings = m_effects->settings();
        settings.bands = preset.bands;
        settings.bass = preset.bass;
        settings.treble = preset.treble;
        m_effects->setSettings(settings);
        showSettings(settings);
        return true;
    }
    showSettings(m_effects->settings()); // "Custom" is not a preset
    return false;
}

void AudioControlDialog::resetToDefault()
{
    m_effects->setSettings({});
    showSettings(m_effects->settings());
}
