#pragma once

#include <memory>

#include "platform/BarPresenter.h"

namespace hyperlight {

// A native NSStatusItem hosting a layer-backed view.
//
// Not QSystemTrayIcon, and this is not a preference. Qt's cocoa plugin calls
// setTemplate: on whatever image it is given, and a template image is a
// monochrome mask -- the violet-to-cyan gradient would arrive in the menu bar
// as a flat silhouette. Going native is the only way to get colour up there.
//
// The corollary, which is easy to miss: we must never set button.image either.
// Leaving it nil and hosting our own view as a subview of the button is the
// arrangement that keeps AppKit from second-guessing what we drew.
class MacBarPresenter : public BarPresenter
{
    Q_OBJECT

public:
    explicit MacBarPresenter(QObject *parent = nullptr);
    ~MacBarPresenter() override;

    bool install() override;
    void remove() override;

    qreal heightPt() const override;
    qreal devicePixelRatio() const override;
    void setWidthPt(qreal widthPt) override;
    void present(const QImage &image) override;
    bool isVisibleOnScreen() const override;
    bool prefersCompact() const override;

    void setPreferredFps(int fps) override;
    void startClock() override;
    void stopClock() override;
    bool isClockRunning() const override;


    void setMenu(const MenuModel &menu) override;
    void showMenu();

    // Called from the display link and the AppKit notifications. Public because
    // an Objective-C helper object has to reach them.
    void emitFrameDue();
    void emitGeometryChanged();
    void emitClicked();
    void emitMenuAction(const QString &actionId);
    void emitMenuAboutToShow();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperlight
