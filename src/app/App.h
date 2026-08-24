#pragma once

#include <memory>

#include <QImage>
#include <QObject>
#include <QThread>

#include "app/AudioController.h"
#include "app/DspWorker.h"
#include "app/Settings.h"
#include "dsp/FrameState.h"
#include "platform/BarPresenter.h"
#include "render/BarRenderer.h"
#include "render/RenderStyle.h"

namespace hyperlight {

class DebugHud;

// The wiring layer. Owns everything, decides nothing that belongs elsewhere.
class App : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        bool windowed = false;    // render into a window instead of the menu bar
        bool hud = false;         // show the diagnostics window
        bool verifyAudio = false; // print levels for a few seconds, then exit
        bool fakeAudio = false;   // drive the DSP from a synthetic sweep
        QString shotPath;         // grab one frame and exit
    };

    explicit App(Options options, QObject *parent = nullptr);
    ~App() override;

    bool start();

private Q_SLOTS:
    void onFrameDue();
    void onGeometryChanged();
    void onPermissionChanged(int state);
    void onAudioBecameActive();
    void onAudioBecameSilent();

private:
    void rebuildStyle();
    void applyFrameRate();
    bool frameIsAtRest(const FrameState &state) const;
    void ensureBuffers();
    bool renderCurrentFrame(QImage **out);
    void runVerifyTick();

    Options m_options;
    Settings m_settings;

    std::unique_ptr<AudioController> m_audio;
    std::unique_ptr<BarPresenter> m_presenter;
    std::unique_ptr<DebugHud> m_hud;

    QThread m_dspThread;
    DspWorker *m_worker = nullptr;   // lives on m_dspThread, owned by it

    BarRenderer m_renderer;
    RenderStyle m_style;

    // Two of them. The macOS presenter hands the bytes to Core Animation
    // without a copy, so the image the compositor is reading must not be the
    // one we are about to paint into.
    QImage m_buffers[2];
    int m_nextBuffer = 0;

    qreal m_heightPt = 24.0;
    qreal m_dpr = 2.0;
    std::uint32_t m_lastPresentedSeq = 0;
    int m_atRestFrames = 0;
    int m_verifyTicks = 0;
};

} // namespace hyperlight
