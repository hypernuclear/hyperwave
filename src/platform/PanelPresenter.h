#pragma once

#include <QString>

#include "audio/AudioDevice.h"
#include "platform/BarPresenter.h"

namespace hyperwave {

// A surface that shows more than bars.
//
// BarPresenter is the whole contract for a strip that draws a QImage and
// nothing else -- the macOS status item, and any minimal surface after it.
// Some surfaces are richer than that: they can retract when idle and they
// carry their own source picker, and the app has to be able to push that
// state at them without knowing which platform's surface it is talking to.
//
// This exists because App.cpp was reaching for MacNotchPresenter directly and
// static_cast-ing to it. That compiles on exactly one platform, and the
// alternative was more #ifdef blocks in a file that already had three. The
// Windows panel is the same shape of surface as the notch panel, so the fix
// is one interface rather than a second set of guards.
//
// Track info and transport used to live here too (setNowPlaying,
// transportRequested); the whole feature was removed rather than ported --
// see the note in docs/windows-backend.md.
//
// Deliberately NOT here: notch geometry. availableWidthPt() and isExpanded()
// describe a panel that hangs off a camera housing and grows on hover, which no
// other surface does; the layout that uses them stays behind Q_OS_MACOS where
// it belongs.
class PanelPresenter : public BarPresenter
{
    Q_OBJECT

public:
    using BarPresenter::BarPresenter;

    // Whether the surface shows itself at all. Idle, a panel may retract so the
    // screen looks stock, while staying alive and reachable.
    virtual void setTabVisible(bool visible) = 0;

    // Fills the surface's own source picker. Separate from the menu because the
    // picker is visible without a click.
    virtual void setDevices(const AudioDeviceList &devices, const QString &current) = 0;
};

} // namespace hyperwave
