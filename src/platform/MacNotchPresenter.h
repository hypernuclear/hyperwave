#pragma once

#include <memory>

#include "audio/AudioDevice.h"
#include "app/NowPlaying.h"
#include "platform/BarPresenter.h"

namespace hyperwave {

// A panel anchored to the camera housing, for Macs that have one.
//
// This exists because of a measured failure, not for decoration. On a 1728pt
// notched display with a normal app's menus on the left and fourteen status
// items on the right, hyperwave's status item gets pushed underneath the notch
// and becomes invisible -- while macOS still cheerfully reports
// statusItem.visible == true. The app runs, the tap works, and the user sees
// nothing and concludes it did not launch.
//
// The space directly below the notch is the one piece of that region no other
// app competes for, which is what makes it a fix rather than another contender.
//
// Two states:
//   Collapsed  a black tab hanging below the notch with the strip inside it,
//              shaped so it reads as part of the housing rather than a window.
//   Expanded   drops down on hover into a larger panel.
class MacNotchPresenter : public BarPresenter
{
    Q_OBJECT

public:
    explicit MacNotchPresenter(QObject *parent = nullptr);
    ~MacNotchPresenter() override;

    // False when this display has no notch and no notch was faked, in which
    // case install() will refuse and the caller should fall back to the status
    // item alone.
    static bool isAvailable();

    // Pretend the main display has a notch, for developing on an external
    // monitor. Geometry matches a 14-inch MacBook Pro.
    static void setFakeNotch(bool enabled);

    bool install() override;
    void remove() override;

    qreal heightPt() const override;
    qreal devicePixelRatio() const override;
    void setWidthPt(qreal widthPt) override;
    void present(const QImage &image) override;
    bool isVisibleOnScreen() const override;
    bool prefersCompact() const override { return false; }

    void setPreferredFps(int fps) override;
    void startClock() override;
    void stopClock() override;
    bool isClockRunning() const override;

    void setMenu(const MenuModel &menu) override;
    // Whether the tab is shown at all.
    //
    // Idle, the tab retracts and only the housing-sized window remains, so the
    // notch looks stock when nothing is playing. The window stays alive and
    // hoverable so the panel is still reachable -- otherwise, on a display
    // crowded enough to hide the status item, there would be no way to open
    // anything at all.
    void setTabVisible(bool visible);
    bool isTabVisible() const;

    // How wide the strip may be inside the tab, in points. The notch decides
    // this, not the style -- unlike a status item, the panel cannot grow.
    qreal availableWidthPt() const;

    bool isExpanded() const;
    void setExpanded(bool expanded);

    // What the expanded panel shows above the bars. An invalid info falls back
    // to the app name alone, which is all the audio process list can offer for
    // anything without a scripting dictionary.
    void setNowPlaying(const NowPlayingInfo &info, const QString &fallbackAppName);

    // Reached from the Objective-C helper.
    void emitFrameDue();
    void emitGeometryChanged();
    void emitClicked();
    void emitMenuAction(const QString &actionId);
    void handleHover(bool inside);
    void handleTransport(int command);
    void handleSourcePick(const QString &uid);
    void setDevices(const AudioDeviceList &devices, const QString &current);
    void showMenu();

Q_SIGNALS:
    void transportRequested(int command);

private:
    void applyFrame();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperwave
