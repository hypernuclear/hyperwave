#pragma once

#include <memory>

#include <QImage>
#include <QObject>

namespace hyperlight {

// The platform seam on the display side.
//
// A presenter owns whatever the OS calls "the thing in the bar" -- an
// NSStatusItem on macOS, a notification-area icon or a docked strip on Windows,
// a StatusNotifierItem on Linux -- and knows how to put a QImage into it. It
// knows nothing about audio.
class BarPresenter : public QObject
{
    Q_OBJECT

public:
    explicit BarPresenter(QObject *parent = nullptr) : QObject(parent) {}
    ~BarPresenter() override = default;

    static std::unique_ptr<BarPresenter> create(QObject *parent = nullptr);

    virtual bool install() = 0;
    virtual void remove() = 0;

    // The strip's logical height in points and the backing scale it should be
    // rendered at. The host owns both: only it knows how tall its bar is.
    virtual qreal heightPt() const = 0;
    virtual qreal devicePixelRatio() const = 0;

    // How wide the host wants the strip to be, in points. Set once at install
    // time and never varied with the audio -- a status item whose width changes
    // relayouts the entire menu bar and reads as a glitch.
    virtual void setWidthPt(qreal widthPt) = 0;

    virtual void present(const QImage &image) = 0;

    // Whether this host would rather have a narrow strip. True on a notched
    // display, where menu bar space is scarce enough that a full-width item can
    // get pushed into the overflow and never be seen at all -- which the user
    // reads as "the app didn't launch".
    virtual bool prefersCompact() const { return false; }

    // Whether the strip is actually on screen. False when the item is hidden,
    // pushed into an overflow, or occluded by a fullscreen app -- all of which
    // are reasons to stop spending cycles drawing.
    virtual bool isVisibleOnScreen() const = 0;

    // Drives the clock. On macOS this is a display link bound to the view's
    // screen; the presenter calls back once per vsync while running.
    // Frames per second the host should aim for. Measured on macOS 26, dropping
    // a menu bar strip from 60 to 30 costs 2.9% of a core instead of 7.2% --
    // better than linear, because most of the per-callback cost is AppKit's
    // display-link dispatch rather than anything we do inside it.
    virtual void setPreferredFps(int fps) = 0;
    virtual void startClock() = 0;
    virtual void stopClock() = 0;
    virtual bool isClockRunning() const = 0;

Q_SIGNALS:
    // Emitted once per display refresh. The app renders in response.
    void frameDue();

    void clicked();
    void geometryChanged();
};

} // namespace hyperlight
