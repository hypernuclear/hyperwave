#pragma once

#include <memory>

#include "platform/BarPresenter.h"

namespace hyperlight {

// A panel anchored to the camera housing, for Macs that have one.
//
// This exists because of a measured failure, not for decoration. On a 1728pt
// notched display with a normal app's menus on the left and fourteen status
// items on the right, hyperlight's status item gets pushed underneath the notch
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

    // How wide the strip may be inside the tab, in points. The notch decides
    // this, not the style -- unlike a status item, the panel cannot grow.
    qreal availableWidthPt() const;

    bool isExpanded() const;
    void setExpanded(bool expanded);

    // Reached from the Objective-C helper.
    void emitFrameDue();
    void emitGeometryChanged();
    void emitClicked();
    void emitMenuAction(const QString &actionId);
    void handleHover(bool inside);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperlight
