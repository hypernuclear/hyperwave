#pragma once

#include <QString>

#include "app/NowPlaying.h"
#include "audio/AudioDevice.h"
#include "platform/BarPresenter.h"

namespace hyperwave {

// A surface that shows more than bars.
//
// BarPresenter is the whole contract for a strip that draws a QImage and
// nothing else -- the macOS status item, and any minimal surface after it. Some
// surfaces are richer than that: they have room for the track, a source picker
// and transport buttons, and the app has to be able to push that state at them
// without knowing which platform's surface it is talking to.
//
// This exists because App.cpp was reaching for MacNotchPresenter directly and
// static_cast-ing to it in four places. That compiles on exactly one platform,
// and the alternative was four more #ifdef blocks in a file that already had
// three. The Windows appbar wants the same four things -- it is a strip with
// space for the track and the source, which is the same shape of surface as the
// notch panel -- so the fix is one interface rather than a second set of
// guards.
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

    // What to show above the bars. An invalid info falls back to the app name,
    // which is all the audio process list can offer for anything without a
    // scripting dictionary.
    virtual void setNowPlaying(const NowPlayingInfo &info,
                               const QString &fallbackAppName) = 0;

    // Fills the surface's own source picker. Separate from the menu because the
    // picker is visible without a click.
    virtual void setDevices(const AudioDeviceList &devices, const QString &current) = 0;

Q_SIGNALS:
    // A transport button was pressed. Carries a TransportCommand as an int so
    // this header does not drag the media layer in behind it.
    void transportRequested(int command);
};

} // namespace hyperwave
