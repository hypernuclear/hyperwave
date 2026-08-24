#include "platform/BarPresenter.h"

// Windows display: NOT IMPLEMENTED.
//
// The placement decision is made, and it is worth recording why the obvious
// options were rejected before someone re-litigates it:
//
//   * Deskbands -- the classic "toolbar inside the taskbar" -- were removed in
//     Windows 11. Not deprecated, removed.
//   * Windows 11 Widgets are Adaptive Cards with a refresh budget measured in
//     minutes. They cannot animate at all, let alone at 60 fps.
//   * An AppBar (SHAppBarMessage) does work and docks a real strip to a screen
//     edge, but it permanently reserves screen space, which is a heavy price
//     for an ambient decoration.
//
// So: an animated notification-area icon as the always-present anchor, plus an
// optional frameless, translucent, click-through strip that snaps just above
// the taskbar and remembers where it was dragged.
//
// Both surfaces render from BarRenderer, unchanged. The tray path converts the
// QImage to an HICON (QPixmap::toWinHICON via QIcon) at whatever the shell asks
// for -- typically 16 or 20 logical pixels, so roughly eight bands rather than
// twenty-eight; RenderStyle already carries bandCount for exactly this.
//
// Two things that will bite:
//   * Windows 11 hides new tray icons behind the overflow chevron by default,
//     so a first run looks like nothing happened. The strip is what makes the
//     app discoverable; the tray icon is what makes it feel native.
//   * There is no display link. Use a timer synchronised to
//     DwmGetCompositionTimingInfo, or accept a plain 60 Hz timer and the
//     occasional dropped frame.
//
// See docs/windows-backend.md.

namespace hyperlight {

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

} // namespace hyperlight
