#include "LaunchAtLogin.h"

// Neither macOS nor Windows. There is no portable answer here — Linux
// alone has several — so report unsupported and let the menu hide the
// item rather than offer a switch that does nothing.

namespace hyperwave::launchAtLogin {

bool supported()          { return false; }
bool isEnabled()          { return false; }
bool setEnabled(bool)     { return false; }

} // namespace hyperwave::launchAtLogin
