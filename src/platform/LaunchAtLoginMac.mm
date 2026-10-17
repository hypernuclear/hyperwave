#include "LaunchAtLogin.h"

#include <QDebug>

#import <ServiceManagement/ServiceManagement.h>

namespace hyperwave::launchAtLogin {

// SMAppService. It replaces the old LSSharedFileList/login-item-helper
// dance entirely and puts the app in
// System Settings > General > Login Items where the user expects to find
// it — including the switch to turn us off, which is the point.
//
// Requires macOS 13+; our deployment target is 14.0.

bool supported()
{
    return true;
}

bool isEnabled()
{
    return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
}

bool setEnabled(bool enabled)
{
    SMAppService *service = SMAppService.mainAppService;
    NSError *error = nil;
    const bool ok = enabled ? [service registerAndReturnError:&error]
                            : [service unregisterAndReturnError:&error];
    if (!ok) {
        qWarning("hyperwave: could not %s login item: %s",
                 enabled ? "register" : "unregister",
                 error ? error.localizedDescription.UTF8String : "unknown error");
    }
    // Report what the SYSTEM now thinks, not what we asked for. Registering
    // can fail quietly for an unsigned or relocated bundle, and a menu
    // that ticks itself anyway would be lying.
    return isEnabled();
}

} // namespace hyperwave::launchAtLogin
