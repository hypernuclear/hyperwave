#pragma once

#include <memory>
#include <vector>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QThread>

#include "app/AudioController.h"
#include "app/DspWorker.h"
#include "app/MenuModel.h"
#include "app/NowPlaying.h"
#include "app/Settings.h"
#include "app/VisualiserControls.h"
#include "dsp/FrameState.h"
#include "platform/BarPresenter.h"
#include "render/BarRenderer.h"
#include "render/RenderStyle.h"
#include "render/GpuPost.h"
#include "render/LineRenderer.h"
#include "render/MirrorRenderer.h"
#include "render/RingRenderer.h"
#include "render/RidgeRenderer.h"
#include "render/StrandRenderer.h"
#include "render/Visualiser.h"

namespace hyperwave {

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
        bool fakeNotch = false;   // pretend the display has a notch
        QString shotPath;         // grab one frame and exit
        QString sourceBundleId;   // listen to one app; "list" prints and exits
        QString vizStyle;         // "bars", "ridge" or "strands"; empty = the setting
        QString gradientId;       // palette preset id; empty = the setting
        qreal previewScale = 1.0; // magnifies the windowed/shot canvas
    };

    // The looks, by id. Kept next to Options because the command line, the
    // settings and the menu all have to agree on the same spelling.
    static std::unique_ptr<Visualiser> makeVisualiser(const QString &id);

    // What a look's glow should be before anyone has touched it. Only the lines
    // arrive with it switched on.
    static RenderStyle::Bloom defaultBloomFor(const QString &id);

    explicit App(Options options, QObject *parent = nullptr);
    ~App() override;

    bool start();

private Q_SLOTS:
    void onMenuAction(const QString &actionId);
    void refreshMenu();
    void refreshNowPlaying();
    void onPermissionChanged(int state);
    void onAudioBecameActive();
    void onAudioBecameSilent();

private:
    // One thing that shows bars. There are up to two: the status item, and on a
    // notched Mac the panel below the housing.
    //
    // They are separate surfaces rather than one presenter with two outputs
    // because they genuinely differ -- different width, different height,
    // different band count, and the notch panel changes size when it expands.
    // What they share is the renderer, which is the whole point.
    struct Surface
    {
        std::unique_ptr<BarPresenter> presenter;
        RenderStyle style;
        // Which look this surface is drawing. Held through the interface so
        // that changing it at runtime is an assignment rather than a rebuild
        // of the surface and its buffers.
        std::unique_ptr<Visualiser> renderer;

        // Two buffers, alternated: on macOS the bytes go to Core Animation
        // without a copy, so the image the compositor is reading must not be
        // the one being painted into.
        QImage buffers[2];
        int nextBuffer = 0;

        qreal heightPt = 22.0;
        qreal dpr = 2.0;
        std::uint32_t lastPresentedSeq = 0;
        int atRestFrames = 0;
        bool isNotch = false;
    };

    void addSurface(std::unique_ptr<BarPresenter> presenter, bool isNotch);
    void onFrameDue(Surface &surface);
    void onGeometryChanged(Surface &surface);
    void rebuildStyle(Surface &surface);
    void ensureBuffers(Surface &surface);
    bool frameIsAtRest(const FrameState &state) const;

    // Re-style, re-size and repaint one surface after something changed under
    // it. onGeometryChanged() cannot serve here: it returns early when there is
    // no presenter, which is exactly what the preview window is.
    void refreshSurface(Surface &surface);
    void connectControls(VisualiserControls *controls);
    // `origin` is the panel the change came from, which must not be seeded
    // back: re-setting a spin box while its text is being edited moves the
    // cursor and reselects, and the field stops behaving like a field.
    void applyVisualiserTuning(VisualiserControls *origin);
    void showSettings();
    QString activeVizId() const;
    RenderStyle::Bloom bloomFor(const QString &id) const;

    void applyFrameRate();
    void setNotchTabVisible(bool visible);
    MenuModel buildMenu() const;
    void showAbout();
    void relaunch();
    void runVerifyTick();

    Options m_options;
    Settings m_settings;

    // Live tuning, owned here rather than rebuilt from defaults every time.
    // rebuildStyle() starts from a fresh RenderStyle on every geometry change,
    // so anything adjusted in the HUD has to be reapplied from somewhere that
    // outlives it, or the first resize would silently undo it.
    RenderStyle::Strands m_strandTuning;
    RenderStyle::Ridge m_ridgeTuning;
    RenderStyle::Lines m_lineTuning;
    RenderStyle::Mirror m_mirrorTuning;
    RenderStyle::Ring m_ringTuning;
    // Keyed by visualiser id, because glow belongs to a look and not to the
    // application. Missing keys fall back to that look's default.
    QHash<QString, RenderStyle::Bloom> m_bloomTuning;
    // Bloom is a post-process over whatever a look drew, so it lives here
    // rather than inside any renderer. One instance: it holds GPU resources
    // sized to the frame, and every surface is the same size at a time.
    GpuPost m_post;

    std::unique_ptr<AudioController> m_audio;
    std::unique_ptr<NowPlaying> m_nowPlaying;
    std::unique_ptr<DebugHud> m_hud;
    // The user-facing settings window. Lazily built, kept alive so it reopens
    // where it was left rather than being rebuilt each time.
    std::unique_ptr<QWidget> m_settingsWindow;
    VisualiserControls *m_settingsControls = nullptr;

    // Deque rather than vector: surfaces hand out references that outlive the
    // call, and a vector reallocating on the second push would dangle them.
    std::vector<std::unique_ptr<Surface>> m_surfaces;
    Surface *m_menuSurface = nullptr;   // the one that carries the menu
    Surface *m_notchSurface = nullptr;

    QThread m_dspThread;
    DspWorker *m_worker = nullptr;   // lives on m_dspThread, owned by it

    int m_verifyTicks = 0;
};

} // namespace hyperwave
