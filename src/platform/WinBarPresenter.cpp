#include "platform/BarPresenter.h"

// Windows display: NOT IMPLEMENTED.
//
// The surface is decided: an application desktop toolbar (SHAppBarMessage),
// docked to a screen edge, plus a static notification-area icon as the anchor.
// It reserves screen space, which is the real cost, but it is the only surface
// that is guaranteed visible -- a floating strip loses to anything topmost, a
// desktop widget loses to the first maximised window -- and ABN_FULLSCREENAPP
// handles games for free. Deskbands, the thing that genuinely lived inside the
// taskbar, were removed in Windows 11; Widgets are Adaptive Cards with a
// refresh budget in minutes and cannot animate at all.
//
// Build the tray icon and menu BEFORE the appbar. First run does not dock, so
// the tray is what offers docking, what undocks again, and what remains when
// ABM_NEW is refused because another appbar owns that edge.
//
// The icon does not animate. At 16-24 px none of the six looks reads, and
// Shell_NotifyIcon is a cross-process call into explorer.exe. Draw it once.
//
// Two things that will bite:
//   * ABM_REMOVE before the window is destroyed is not optional. An appbar that
//     fails to unregister leaves its reserved strip behind after the process is
//     gone, and the way back is a reboot. Test the crash path deliberately.
//   * There is no display link. Use a timer synchronised to
//     DwmGetCompositionTimingInfo, or accept a plain 60 Hz timer and the
//     occasional dropped frame.
//
// This class implements BarPresenter, which is the bare strip contract. The
// appbar will also want the track, the source picker and transport, so it
// should implement PanelPresenter instead -- the same interface the macOS notch
// panel uses, which exists so App.cpp does not need a platform branch per call.
//
// See docs/windows-backend.md.

namespace hyperwave {

namespace {

class WinBarPresenter : public BarPresenter
{
public:
    explicit WinBarPresenter(QObject *parent) : BarPresenter(parent) {}

    bool install() override { return false; }
    void remove() override {}

    qreal heightPt() const override { return 20.0; }
    qreal devicePixelRatio() const override { return 1.0; }
    void setWidthPt(qreal) override {}
    void present(const QImage &) override {}
    bool isVisibleOnScreen() const override { return false; }

    void setPreferredFps(int) override {}
    void startClock() override {}
    void stopClock() override {}
    bool isClockRunning() const override { return false; }
};

} // namespace

std::unique_ptr<BarPresenter> BarPresenter::create(QObject *parent)
{
    return std::make_unique<WinBarPresenter>(parent);
}

} // namespace hyperwave
