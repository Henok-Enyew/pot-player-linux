#include "EqualizerDialog.h"
#include "Equalizer.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString gainText(double gain)
{
    return QStringLiteral("%1%2").arg(gain > 0 ? QStringLiteral("+") : QString()).arg(gain, 0, 'f', 0);
}

} // namespace

EqualizerDialog::EqualizerDialog(Equalizer *equalizer, QWidget *parent)
    : QDialog(parent)
    , m_equalizer(equalizer)
    , m_presets(new QComboBox(this))
{
    setObjectName(QStringLiteral("EqualizerDialog"));
    setWindowTitle(tr("Equalizer"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Preset:"), this));
    m_presets->setObjectName(QStringLiteral("EqualizerPresets"));
    for (const Equalizer::Preset &preset : Equalizer::presets())
        m_presets->addItem(preset.name, preset.id);
    m_presets->addItem(tr("Custom"), QStringLiteral("custom"));
    top->addWidget(m_presets, 1);
    auto *reset = new QPushButton(tr("Reset"), this);
    reset->setObjectName(QStringLiteral("EqualizerReset"));
    reset->setAutoDefault(false);
    top->addWidget(reset);
    layout->addLayout(top);
    layout->addSpacing(8);

    auto *bands = new QGridLayout;
    bands->setHorizontalSpacing(10);
    bands->setVerticalSpacing(4);
    for (int band = 0; band < Equalizer::kBandCount; ++band) {
        auto *gain = new QLabel(this);
        gain->setProperty("role", QStringLiteral("gain"));
        gain->setAlignment(Qt::AlignCenter);
        gain->setMinimumWidth(QFontMetrics(gain->font()).horizontalAdvance(QStringLiteral("+12")) + 4);

        auto *slider = new QSlider(Qt::Vertical, this);
        slider->setObjectName(QStringLiteral("EqualizerBand%1").arg(band));
        slider->setRange(-static_cast<int>(Equalizer::kMaxGain), static_cast<int>(Equalizer::kMaxGain));
        slider->setPageStep(3);
        slider->setMinimumHeight(150);
        slider->setAccessibleName(tr("%1 Hz").arg(Equalizer::bandLabel(band)));

        auto *frequency = new QLabel(Equalizer::bandLabel(band), this);
        frequency->setProperty("role", QStringLiteral("band"));
        frequency->setAlignment(Qt::AlignCenter);

        bands->addWidget(gain, 0, band, Qt::AlignHCenter);
        bands->addWidget(slider, 1, band, Qt::AlignHCenter);
        bands->addWidget(frequency, 2, band, Qt::AlignHCenter);
        m_sliders.append(slider);
        m_gainLabels.append(gain);

        connect(slider, &QSlider::valueChanged, this, [this, band](int value) { m_equalizer->setGain(band, value); });
    }
    layout->addLayout(bands);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // The style's stock icons don't fit the flat skin.
    for (QAbstractButton *button : buttons->buttons())
        button->setIcon(QIcon());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addSpacing(6);
    layout->addWidget(buttons);

    connect(m_presets, &QComboBox::activated, this, [this](int index) {
        const QString id = m_presets->itemData(index).toString();
        if (id != QLatin1String("custom"))
            m_equalizer->setPreset(id);
    });
    connect(reset, &QPushButton::clicked, this, [this] { m_equalizer->setPreset(QStringLiteral("flat")); });
    connect(m_equalizer, &Equalizer::changed, this, &EqualizerDialog::sync);
    sync();
}

void EqualizerDialog::sync()
{
    for (int band = 0; band < Equalizer::kBandCount; ++band) {
        const double gain = m_equalizer->gain(band);
        const QSignalBlocker blocker(m_sliders[band]);
        m_sliders[band]->setValue(static_cast<int>(std::lround(gain)));
        m_gainLabels[band]->setText(gainText(gain));
    }
    const QSignalBlocker blocker(m_presets);
    m_presets->setCurrentIndex(m_presets->findData(m_equalizer->presetId()));
}
