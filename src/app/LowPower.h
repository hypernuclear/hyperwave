#pragma once

namespace hyperlight {

// True when the machine is in Low Power Mode.
//
// Its own tiny header because it is the one power question the app asks and the
// answer is per-platform: NSProcessInfo on macOS, and something else entirely
// everywhere else. Returning false when we cannot tell is the right default --
// it means the visualiser looks its best unless the system has explicitly asked
// everyone to back off.
bool isLowPowerMode();

} // namespace hyperlight
