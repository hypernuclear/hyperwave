#include "app/Shell.h"

#include <QCoreApplication>
#include <QProcess>

namespace hyperwave {

// No portable equivalent. Windows exposes a power-saver state through
// SYSTEM_POWER_STATUS and PowerSettingRegisterNotification, and Linux desktops
// vary; until one of those is wired up, look our best.
bool isLowPowerMode()
{
    return false;
}

void activateApp()
{
    // Nothing to do: outside macOS a window that is shown is a window that has
    // focus, and there is no Dock-icon-less activation policy to work around.
}

void relaunchApp()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            QCoreApplication::arguments().mid(1));
    QCoreApplication::quit();
}

void hideWindowTitle(QWindow *)
{
}

} // namespace hyperwave
