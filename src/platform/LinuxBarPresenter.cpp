#include "platform/BarPresenter.h"

// Linux display: NOT IMPLEMENTED.
//
// QSystemTrayIcon is the right vehicle here, and unlike on macOS it is not
// booby-trapped: the Qt XCB/Wayland backends publish a StatusNotifierItem and
// pass the icon through without any template flattening, so the gradient
// arrives intact. KDE and most Qt-based desktops show it directly; GNOME needs
// the AppIndicator extension, which is worth saying out loud in the README
// rather than letting people discover it.
//
// The frameless strip from the Windows plan is the fallback for desktops with
// no usable tray, and it is the same code either way -- BarRenderer output into
// a translucent always-on-top window.
//
// No display link here either. Qt does expose the compositor's refresh rate via
// QScreen::refreshRate, which is enough to pick a sensible timer interval.

namespace hyperwave {

namespace {

class LinuxBarPresenter : public BarPresenter
{
public:
    explicit LinuxBarPresenter(QObject *parent) : BarPresenter(parent) {}

    bool install() override { return false; }
    void remove() override {}

    qreal heightPt() const override { return 22.0; }
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
    return std::make_unique<LinuxBarPresenter>(parent);
}

} // namespace hyperwave
