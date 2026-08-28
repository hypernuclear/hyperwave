// The create() that runs when there is no updater to create.
//
// Compiled on platforms Sparkle and WinSparkle do not cover, on any build
// configured with HYPERWAVE_AUTO_UPDATE=OFF, and on every build made without an
// EdDSA public key -- see the guard in CMakeLists.txt. Returning null is the
// documented answer; callers hide their update UI and carry on.
#include "update/AppUpdater.h"

namespace hyperwave {

AppUpdater *AppUpdater::create(QObject *) { return nullptr; }

} // namespace hyperwave
