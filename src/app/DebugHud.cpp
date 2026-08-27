#include "app/DebugHud.h"

#include <algorithm>

#include <cmath>

#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include "audio/AudioSource.h"
#include "dsp/DspConfig.h"

namespace hyperwave {

namespace {

QString permissionName(int state)
{
    switch (static_cast<PermissionState>(state)) {
    case PermissionState::Unknown: return QStringLiteral("unknown");
    case PermissionState::Probing: return QStringLiteral("probing");
    case PermissionState::Granted: return QStringLiteral("granted");
    case PermissionState::LikelyDenied: return QStringLiteral("likely denied");
    case PermissionState::Unsupported: return QStringLiteral("unsupported");
    }
    return QStringLiteral("?");
}

// Twenty-eight bars of ASCII. Crude on purpose: it makes the DSP legible
// before a single pixel of the real renderer exists, which is the order the
// two are meant to be built in.
QString asciiBars(const FrameState &state)
{
    static const char *kRamp = " .:-=+*#%@";
    QString out;
    out.reserve(state.bandCount);
    for (int k = 0; k < state.bandCount; ++k) {
        const int level = qBound(0, static_cast<int>(state.bars[k] * 9.0f), 9);
        out.append(QLatin1Char(kRamp[level]));
    }
    return out;
}

} // namespace

DebugHud::DebugHud(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("hyperwave — debug"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_preview = new QLabel(this);
    m_preview->setMinimumHeight(72);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setAutoFillBackground(true);
    m_preview->setStyleSheet(QStringLiteral("background: #101014; border-radius: 6px;"));
    layout->addWidget(m_preview);

    QFont mono(QStringLiteral("SF Mono"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(12);

    m_bars = new QLabel(this);
    m_bars->setFont(mono);
    layout->addWidget(m_bars);

    m_stats = new QLabel(this);
    m_stats->setFont(mono);
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_stats);

    // --- live controls -------------------------------------------------------
    //
    // Everything below takes effect on the next frame. What it replaces was a
    // command-line flag per parameter and one process per combination, which is
    // a bad loop for anything that has to be judged by looking at it.
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

    m_scale = new QSpinBox(this);
    m_scale->setRange(1, 16);
    m_scale->setSuffix(QStringLiteral("x"));
    connect(m_scale, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_seeding)
            Q_EMIT previewScaleRequested(v);
    });
    lookRow->addRow(QStringLiteral("Magnify"), m_scale);
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
                    // slider steps across 0.1..4.0 lands on 0.599 for what is
                    // plainly 0.6, and a readout that disagrees with the
                    // default it is showing reads as a bug in the default.
                    readout->setText(QString::number(*value, 'f', 2));
                    if (!m_seeding)
                        onChange();
                });
        form->addRow(label, row);
        m_seeders.push_back([slider, value, min, max] {
            slider->setValue(
                static_cast<int>(std::lround((*value - min) / (max - min) * 1000.0)));
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

    showRelevantControls();

    layout->addStretch();
    resize(620, 780);
}

void DebugHud::showRelevantControls()
{
    const QString id = m_look ? m_look->currentData().toString() : QString();
    if (m_strandBox)
        m_strandBox->setVisible(id == QLatin1String("strands"));
    if (m_ridgeBox)
        m_ridgeBox->setVisible(id == QLatin1String("ridge"));
}

void DebugHud::setVisualiser(const QString &id)
{
    if (!m_look)
        return;
    m_seeding = true;
    const int index = m_look->findData(id.isEmpty() ? QStringLiteral("bars") : id);
    m_look->setCurrentIndex(index >= 0 ? index : 0);
    m_seeding = false;
    showRelevantControls();
}

void DebugHud::setPreviewScale(qreal scale)
{
    if (!m_scale)
        return;
    m_seeding = true;
    m_scale->setValue(std::clamp(static_cast<int>(std::lround(scale)), 1, 16));
    m_seeding = false;
}

void DebugHud::setStrands(const RenderStyle::Strands &strands)
{
    m_strands = strands;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}

void DebugHud::setRidge(const RenderStyle::Ridge &ridge)
{
    m_ridge = ridge;
    m_seeding = true;
    for (const auto &seed : m_seeders)
        seed();
    m_seeding = false;
}

void DebugHud::setPreview(const QImage &image)
{
    if (image.isNull())
        return;
    // Magnify enough to be legible, but not past a window that fits on screen.
    //
    // A fixed 3x was right when the only thing to look at was a 236x44 strip.
    // With --preview-scale the incoming image is already large, and tripling a
    // 1416px render produces a four-thousand-pixel window that cannot be seen
    // at all. Nearest-neighbour still, because the whole point of magnifying
    // is to show the pixel-level detail a smooth filter would hide.
    constexpr int kMaxPreviewWidth = 1200;
    const int factor = std::max(1, kMaxPreviewWidth / std::max(1, image.width()));
    m_preview->setPixmap(QPixmap::fromImage(image).scaled(
        image.size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation));
}

void DebugHud::setFrame(const FrameState &state)
{
    m_frame = state;
    refresh();
}

void DebugHud::refresh()
{
    m_bars->setText(QStringLiteral("[%1]").arg(asciiBars(m_frame)));

    QString text =
        QStringLiteral("frame     %1\n"
                       "rms       %2 dB   (silence below %3)\n"
                       "agc       %4 dB\n"
                       "presence  %5\n"
                       "epoch     %6\n"
                       "state     %7")
            .arg(m_frame.seq, 8)
            .arg(static_cast<double>(m_frame.rmsDb), 8, 'f', 1)
            .arg(static_cast<double>(dsp::kSilenceRmsDb), 0, 'f', 0)
            .arg(static_cast<double>(m_frame.agcDb), 8, 'f', 1)
            .arg(static_cast<double>(m_frame.presence), 8, 'f', 2)
            .arg(m_frame.formatEpoch, 8)
            .arg(permissionName(m_frame.permission));

    if (m_source) {
        text += QStringLiteral("\n"
                               "rate      %1 Hz\n"
                               "callbacks %2\n"
                               "overruns  %3\n"
                               "capture   %4")
                    .arg(m_source->sampleRate(), 8, 'f', 0)
                    .arg(m_source->callbackCount(), 8)
                    .arg(m_source->overrunCount(), 8)
                    .arg(m_source->statusDetail());
    }

    m_stats->setText(text);
}

} // namespace hyperwave
