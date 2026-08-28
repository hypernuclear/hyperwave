// A stable, anonymous identifier for this machine.
//
// The same algorithm the sibling products use, deliberately: hardware id where
// the platform offers one, hostname plus primary MAC where it does not, and
// SHA-256 over the result. A machine therefore reports the same id to each of
// them.
//
// That last property is a choice, and it cuts both ways. It is what lets you ask
// whether one product's audience overlaps another's, and it is also a
// correlation the user did not ask for. It happens only while analytics is on,
// which is what the switch is for.
//
// The hash is one-way and the raw hardware id never leaves the machine, so what
// reaches the server distinguishes installs without identifying a person or a
// device.
//
// Lives under analytics/ because analytics is its only consumer. If a second one
// ever appears -- licensing, say -- move it up.
#pragma once

#include <QString>

namespace hyperwave {

/// The id, computed once and cached for the process lifetime.
QString machineId();

} // namespace hyperwave
