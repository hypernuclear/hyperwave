#include "app/DebugHud.h"

#include <algorithm>

#include <QFont>
#include <QLabel>
#include <QPixmap>
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

    layout->addStretch();
    resize(560, 320);
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
