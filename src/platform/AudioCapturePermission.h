#pragma once

namespace hyperwave {

// The two things the app needs to do about the system-audio grant that require
// AppKit: send the user to the right place, and notice when they come back.
namespace AudioCapturePermission {

// Opens System Settings at Privacy & Security > "System Audio Recording Only".
//
// The anchor is Privacy_AudioCapture; if a future release renames it the URL
// still opens the Privacy pane rather than failing, which is the right way to
// degrade.
void openSystemSettings();

// Installs a one-shot hook that fires the next time the app becomes active.
// Used to re-probe after we have sent someone to Settings, since a grant made
// over there produces no notification of any kind on our side.
void notifyOnNextActivation(void (*callback)(void *), void *context);

} // namespace AudioCapturePermission

} // namespace hyperwave
