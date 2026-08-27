#include "app/VisualiserControls.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
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
    m_look->addItem(QStringLiteral("Mirror"), QStringLiteral("mirror"));
    m_look->addItem(QStringLiteral("Lines"), QStringLiteral("lines"));
    m_look->addItem(QStringLiteral("Ring"), QStringLiteral("ring"));
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
                    // While seeding, the slider is being told what the value is
                    // and has nothing to say about it. Writing back here would
                    // replace the authored number with whichever of a thousand
                    // steps is nearest -- 0.12 became 0.11998 and 0.8 became
                    // 0.7981, and those degraded values then got saved the next
                    // time any other control moved.
                    if (!m_seeding) {
                        *value = min + (max - min) * (raw / 1000.0);
                    }
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
        // Commit on Enter or on leaving the field, not on every keystroke.
        //
        // With tracking on, typing "64" fires valueChanged at "6" as well,
        // which restyles every surface and -- worse -- comes back round as a
        // re-seed that rewrites the text under the cursor. The field then
        // fights whatever you type and never seems to let go.
        spin->setKeyboardTracking(false);
        connect(spin, &QSpinBox::valueChanged, this, [this, value, onChange](int v) {
            if (m_seeding)
                return;   // being told, not telling; same reason as the sliders
            *value = v;
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

    const std::function<void()> linesChanged = [this] { Q_EMIT linesRequested(m_lines); };
    m_lineBox = new QGroupBox(QStringLiteral("Lines"), this);
    auto *lineForm = new QFormLayout(m_lineBox);
    lineForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    lineForm->setLabelAlignment(Qt::AlignRight);
    addSpin(lineForm, QStringLiteral("Count"), 4, 512, &m_lines.count, linesChanged);
    addSlider(lineForm, QStringLiteral("Width pt"), 0.2, 4.0, &m_lines.widthPt, linesChanged);
    addSlider(lineForm, QStringLiteral("Inset"), 0.0, 0.4, &m_lines.verticalInset, linesChanged);
    addSlider(lineForm, QStringLiteral("Height response"), 0.0, 1.0, &m_lines.heightResponse,
              linesChanged);
    addSlider(lineForm, QStringLiteral("Floor"), 0.0, 0.5, &m_lines.floorLevel, linesChanged);
    addSlider(lineForm, QStringLiteral("Gamma"), 0.5, 4.0, &m_lines.gamma, linesChanged);
    addSlider(lineForm, QStringLiteral("Attack"), 0.05, 1.0, &m_lines.attack, linesChanged);
    addSlider(lineForm, QStringLiteral("Release"), 0.02, 1.0, &m_lines.release, linesChanged);
    layout->addWidget(m_lineBox);
    const std::function<void()> mirrorChanged = [this] { Q_EMIT mirrorRequested(m_mirror); };
    m_mirrorBox = new QGroupBox(QStringLiteral("Mirror"), this);
    auto *mirrorForm = new QFormLayout(m_mirrorBox);
    mirrorForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    mirrorForm->setLabelAlignment(Qt::AlignRight);
    addSpin(mirrorForm, QStringLiteral("Count"), 4, 512, &m_mirror.count, mirrorChanged);
    addSlider(mirrorForm, QStringLiteral("Width pt"), 0.3, 6.0, &m_mirror.widthPt, mirrorChanged);
    addSlider(mirrorForm, QStringLiteral("Inset"), 0.0, 0.4, &m_mirror.verticalInset,
              mirrorChanged);
    addSlider(mirrorForm, QStringLiteral("Stiffness"), 0.05, 0.8, &m_mirror.stiffness,
              mirrorChanged);
    addSlider(mirrorForm, QStringLiteral("Bounce"), 0.60, 0.98, &m_mirror.damping, mirrorChanged);
    addSlider(mirrorForm, QStringLiteral("Rest height pt"), 0.0, 3.0, &m_mirror.minHalfPt,
              mirrorChanged);
    layout->addWidget(m_mirrorBox);
    const std::function<void()> ringChanged = [this] { Q_EMIT ringRequested(m_ring); };
    m_ringBox = new QGroupBox(QStringLiteral("Ring"), this);
    auto *ringForm = new QFormLayout(m_ringBox);
    ringForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    ringForm->setLabelAlignment(Qt::AlignRight);
    addSpin(ringForm, QStringLiteral("Segments"), 24, 720, &m_ring.segments, ringChanged);
    addSlider(ringForm, QStringLiteral("Base radius"), 0.1, 0.9, &m_ring.baseRadius, ringChanged);
    addSlider(ringForm, QStringLiteral("Reach"), 0.05, 0.8, &m_ring.reach, ringChanged);
    addSlider(ringForm, QStringLiteral("Line width pt"), 0.3, 4.0, &m_ring.lineWidthPt,
              ringChanged);
    addSlider(ringForm, QStringLiteral("Separation"), 0.0, 0.8, &m_ring.separation, ringChanged);
    addSlider(ringForm, QStringLiteral("Offset"), 0.0, 0.15, &m_ring.constantOffset, ringChanged);
    addSlider(ringForm, QStringLiteral("Fill"), 0.0, 1.0, &m_ring.fillOpacity, ringChanged);
    addSlider(ringForm, QStringLiteral("Margin"), 0.0, 0.4, &m_ring.margin, ringChanged);
    addSlider(ringForm, QStringLiteral("Layer opacity"), 0.1, 1.0, &m_ring.layerOpacity,
              ringChanged);
    addSlider(ringForm, QStringLiteral("Stiffness"), 0.05, 0.8, &m_ring.stiffness, ringChanged);
    addSlider(ringForm, QStringLiteral("Bounce"), 0.60, 0.98, &m_ring.damping, ringChanged);
    layout->addWidget(m_ringBox);
    // Bloom is not tied to a look, so its group is always visible.
    const std::function<void()> bloomChanged = [this] { Q_EMIT bloomRequested(m_bloom); };
    auto *bloomBox = new QGroupBox(QStringLiteral("Glow"), this);
    auto *bloomForm = new QFormLayout(bloomBox);
    bloomForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    bloomForm->setLabelAlignment(Qt::AlignRight);
    auto *bloomOn = new QCheckBox(QStringLiteral("Enabled"), this);
    connect(bloomOn, &QCheckBox::toggled, this, [this, bloomChanged](bool on) {
        if (m_seeding)
            return;
        m_bloom.enabled = on;
        bloomChanged();
    });
    bloomForm->addRow(QString(), bloomOn);
    m_seeders.push_back([this, bloomOn] { bloomOn->setChecked(m_bloom.enabled); });
    addSlider(bloomForm, QStringLiteral("Threshold"), 0.0, 0.8, &m_bloom.threshold, bloomChanged);
    addSlider(bloomForm, QStringLiteral("Knee"), 0.0, 0.8, &m_bloom.knee, bloomChanged);
    addSlider(bloomForm, QStringLiteral("Intensity"), 0.0, 4.0, &m_bloom.intensity, bloomChanged);
    addSlider(bloomForm, QStringLiteral("Radius"), 0.5, 8.0, &m_bloom.radius, bloomChanged);
    addSlider(bloomForm, QStringLiteral("Dispersion"), 0.0, 0.15, &m_bloom.dispersion,
              bloomChanged);
    addSlider(bloomForm, QStringLiteral("Rays"), 0.0, 0.4, &m_bloom.radialBlur, bloomChanged);
    addSlider(bloomForm, QStringLiteral("Ray length"), 0.0, 1.0, &m_bloom.rayLength,
              bloomChanged);
    addSlider(bloomForm, QStringLiteral("Ray decay"), 0.70, 0.99, &m_bloom.rayDecay,
              bloomChanged);
    layout->addWidget(bloomBox);
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
    if (m_lineBox)
        m_lineBox->setVisible(id == QLatin1String("lines"));
    if (m_mirrorBox)
        m_mirrorBox->setVisible(id == QLatin1String("mirror"));
    if (m_ringBox)
        m_ringBox->setVisible(id == QLatin1String("ring"));
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
void VisualiserControls::setLines(const RenderStyle::Lines &lines)
{
    m_lines = lines;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}
void VisualiserControls::setMirror(const RenderStyle::Mirror &mirror)
{
    m_mirror = mirror;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}
void VisualiserControls::setRing(const RenderStyle::Ring &ring)
{
    m_ring = ring;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}
void VisualiserControls::setBloom(const RenderStyle::Bloom &bloom)
{
    m_bloom = bloom;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}

} // namespace hyperwave
