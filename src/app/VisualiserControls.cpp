#include "app/VisualiserControls.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace hyperwave {

VisualiserControls::VisualiserControls(Options options, QWidget *parent)
    : QWidget(parent)
    , m_options(options)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *lookRow = new QFormLayout;
    lookRow->setContentsMargins(0, 0, 0, 0);
    lookRow->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    lookRow->setLabelAlignment(Qt::AlignRight);

    m_look = new QComboBox(this);
    m_look->addItem(QStringLiteral("Bars"), QStringLiteral("bars"));
    m_look->addItem(QStringLiteral("Strands"), QStringLiteral("strands"));
    m_look->addItem(QStringLiteral("Terrain"), QStringLiteral("ridge"));
    connect(m_look, &QComboBox::currentIndexChanged, this, [this](int) {
        showRelevantControls();
        if (!m_seeding)
            Q_EMIT visualiserRequested(m_look->currentData().toString());
    });
    lookRow->addRow(QStringLiteral("Look"), m_look);

    if (m_options.showMagnify) {
        m_scale = new QSpinBox(this);
        m_scale->setRange(1, 16);
        m_scale->setSuffix(QStringLiteral("x"));
        connect(m_scale, &QSpinBox::valueChanged, this, [this](int v) {
            if (!m_seeding)
                Q_EMIT previewScaleRequested(v);
        });
        lookRow->addRow(QStringLiteral("Magnify"), m_scale);
    }
    layout->addLayout(lookRow);

    // A slider bound to one qreal. Sliders are integer, so each carries a
    // thousand steps across its range and converts on the way in and out.
    const auto addSlider = [this](QFormLayout *form, const QString &label, qreal min, qreal max,
                                  qreal *value, std::function<void()> onChange) {
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 1000);
        slider->setMinimumWidth(220);
        auto *readout = new QLabel(row);
        readout->setMinimumWidth(52);
        readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(readout);
        connect(slider, &QSlider::valueChanged, this,
                [this, min, max, value, readout, onChange](int raw) {
                    *value = min + (max - min) * (raw / 1000.0);
                    // Two decimals, not three significant figures: a thousand
                    // steps across 0.1..4.0 lands on 0.599 for what is plainly
                    // 0.6, and a readout that contradicts the default it is
                    // showing reads as a bug in the default.
                    readout->setText(QString::number(*value, 'f', 2));
                    if (!m_seeding)
                        onChange();
                });
        form->addRow(label, row);
        m_seeders.push_back([slider, readout, value, min, max] {
            slider->setValue(
                static_cast<int>(std::lround((*value - min) / (max - min) * 1000.0)));
            // The readout is written here as well as in the valueChanged
            // handler, because setValue() on a slider that is already at that
            // position emits nothing. Any parameter seeded to the bottom of its
            // range -- thickness at 0.1 of 0.1..4.0 -- then showed a blank
            // label next to a slider that was perfectly correct.
            readout->setText(QString::number(*value, 'f', 2));
        });
    };

    const auto addSpin = [this](QFormLayout *form, const QString &label, int min, int max,
                                int *value, std::function<void()> onChange) {
        auto *spin = new QSpinBox(this);
        spin->setRange(min, max);
        connect(spin, &QSpinBox::valueChanged, this, [this, value, onChange](int v) {
            *value = v;
            if (!m_seeding)
                onChange();
        });
        form->addRow(label, spin);
        m_seeders.push_back([spin, value] { spin->setValue(*value); });
    };

    const std::function<void()> strandsChanged = [this] { Q_EMIT strandsRequested(m_strands); };
    m_strandBox = new QGroupBox(QStringLiteral("Strands"), this);
    auto *strandForm = new QFormLayout(m_strandBox);
    strandForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    strandForm->setLabelAlignment(Qt::AlignRight);
    addSpin(strandForm, QStringLiteral("Count"), 1, 2048, &m_strands.count, strandsChanged);
    addSlider(strandForm, QStringLiteral("Radius"), 0.01, 0.40, &m_strands.radius, strandsChanged);
    addSlider(strandForm, QStringLiteral("Thickness pt"), 0.1, 4.0, &m_strands.thicknessPt,
              strandsChanged);
    addSlider(strandForm, QStringLiteral("Fill"), 0.4, 4.0, &m_strands.fill, strandsChanged);
    addSlider(strandForm, QStringLiteral("Tilt"), 0.0, 80.0, &m_strands.tiltDegrees,
              strandsChanged);
    addSlider(strandForm, QStringLiteral("Twists"), 0.0, 4.0, &m_strands.twists, strandsChanged);
    addSlider(strandForm, QStringLiteral("Spin"), 0.0, 2.0, &m_strands.spinRate, strandsChanged);
    addSlider(strandForm, QStringLiteral("Opacity"), 0.05, 1.0, &m_strands.opacity,
              strandsChanged);
    addSlider(strandForm, QStringLiteral("Audio gain"), 0.5, 4.0, &m_strands.audioGain,
              strandsChanged);
    layout->addWidget(m_strandBox);

    const std::function<void()> ridgeChanged = [this] { Q_EMIT ridgeRequested(m_ridge); };
    m_ridgeBox = new QGroupBox(QStringLiteral("Terrain"), this);
    auto *ridgeForm = new QFormLayout(m_ridgeBox);
    ridgeForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    ridgeForm->setLabelAlignment(Qt::AlignRight);
    addSpin(ridgeForm, QStringLiteral("Rows"), 2, 96, &m_ridge.rows, ridgeChanged);
    addSpin(ridgeForm, QStringLiteral("Subdivision"), 1, 8, &m_ridge.subdivision, ridgeChanged);
    addSpin(ridgeForm, QStringLiteral("Column stride"), 0, 24, &m_ridge.columnStride,
            ridgeChanged);
    addSlider(ridgeForm, QStringLiteral("Depth"), 0.2, 4.0, &m_ridge.depth, ridgeChanged);
    addSlider(ridgeForm, QStringLiteral("Amplitude"), 0.1, 1.0, &m_ridge.amplitude, ridgeChanged);
    addSlider(ridgeForm, QStringLiteral("Line width pt"), 0.2, 3.0, &m_ridge.lineWidthPt,
              ridgeChanged);
    layout->addWidget(m_ridgeBox);

    if (m_options.showReset) {
        auto *reset = new QPushButton(QStringLiteral("Reset to defaults"), this);
        connect(reset, &QPushButton::clicked, this, [this] { Q_EMIT resetRequested(); });
        auto *resetRow = new QHBoxLayout;
        resetRow->addStretch();
        resetRow->addWidget(reset);
        layout->addLayout(resetRow);
    }

    showRelevantControls();
}

void VisualiserControls::showRelevantControls()
{
    const QString id = m_look ? m_look->currentData().toString() : QString();
    if (m_strandBox)
        m_strandBox->setVisible(id == QLatin1String("strands"));
    if (m_ridgeBox)
        m_ridgeBox->setVisible(id == QLatin1String("ridge"));
}

void VisualiserControls::setVisualiser(const QString &id)
{
    if (!m_look)
        return;
    m_seeding = true;
    const int index = m_look->findData(id.isEmpty() ? QStringLiteral("bars") : id);
    m_look->setCurrentIndex(index >= 0 ? index : 0);
    m_seeding = false;
    showRelevantControls();
}

void VisualiserControls::setPreviewScale(qreal scale)
{
    if (!m_scale)
        return;
    m_seeding = true;
    m_scale->setValue(std::clamp(static_cast<int>(std::lround(scale)), 1, 16));
    m_seeding = false;
}

void VisualiserControls::setStrands(const RenderStyle::Strands &strands)
{
    m_strands = strands;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}

void VisualiserControls::setRidge(const RenderStyle::Ridge &ridge)
{
    m_ridge = ridge;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}

} // namespace hyperwave
