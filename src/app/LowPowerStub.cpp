#include "app/LowPower.h"
namespace hyperlight {
// No portable equivalent. Windows has a power-saver state via
// SYSTEM_POWER_STATUS / PowerSettingRegisterNotification, and Linux desktops
// vary; until one of those is wired up, look our best.
bool isLowPowerMode()
{
    return false;
}
} // namespace hyperlight
