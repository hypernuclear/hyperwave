#pragma once

class QWindow;

namespace hyperwave {

// The handful of questions this app has to ask the operating system that Qt
// either does not answer or answers wrongly for a background agent. Small on
// purpose: anything that grows past a few lines belongs in src/platform/.

// True when the machine is in Low Power Mode. Returning false when we cannot
// tell is the right default -- it means the visualiser looks its best unless
// the system has explicitly asked everyone to back off.
bool isLowPowerMode();

// Bring this app to the front.
//
// An LSUIElement app has no Dock icon and no application menu, and a dialog
// opened from a status item will otherwise appear behind whatever was already
// frontmost. Qt has no equivalent, because Qt assumes an app that shows a
// window wants a Dock icon to go with it.
void activateApp();

// Relaunch, replacing this process.
//
// Exists for one situation: a system-audio grant made while the app was already
// running does not always reach an existing tap, and after rebuilding the
// aggregate and then the tap have both failed, a restart is the only thing left
// that is guaranteed to work.
void relaunchApp();

// Keep a window's title (for Mission Control, the Window menu and VoiceOver)
// but don't draw it in the title bar: for the welcome window, whose artwork
// runs up under the bar, where the text sat on the illustration. Nothing to do
// off macOS, where the title bar is the system's own and readable.
void hideWindowTitle(QWindow *window);

} // namespace hyperwave
