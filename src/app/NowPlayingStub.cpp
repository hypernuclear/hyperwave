#include "app/NowPlaying.h"

namespace hyperlight {

// Nothing here yet. Windows has GlobalSystemMediaTransportControlsSessionManager
// in WinRT, which is the closest thing to what MediaRemote used to be on macOS
// and is not entitlement-gated; Linux has MPRIS over D-Bus, which is better than
// either. Both are a better story than AppleScript -- neither needs a per-app
// permission and neither is limited to applications that shipped a scripting
// dictionary.

NowPlaying::NowPlaying(QObject *parent)
    : QObject(parent)
{
}

bool NowPlaying::isSupported(const QString &)
{
    return false;
}

void NowPlaying::setBundleId(const QString &bundleId)
{
    m_bundleId = bundleId;
}

void NowPlaying::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void NowPlaying::poll()
{
}

void NowPlaying::sendCommand(TransportCommand)
{
}

} // namespace hyperlight
