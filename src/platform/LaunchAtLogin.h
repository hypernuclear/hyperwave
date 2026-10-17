// "Open at Login", behind one interface.
//
// The OS is the source of truth here, not our settings file: the user can
// turn this off in System Settings or delete the Startup shortcut, and a
// cached copy would then be a lie. Every read asks the system.
#pragma once

namespace hyperwave::launchAtLogin {

/// False where we have no implementation, so the menu item can be hidden
/// rather than shown doing nothing.
bool supported();

/// Is the app currently registered to start at login?
bool isEnabled();

/// Returns the state that actually resulted, which is not always the one
/// asked for — macOS can refuse, and the Startup folder can be
/// unwritable. Callers should reflect the return value in the UI rather
/// than assuming the request succeeded.
bool setEnabled(bool enabled);

} // namespace hyperwave::launchAtLogin
