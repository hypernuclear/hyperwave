#include "app/LowPower.h"
#import <Foundation/Foundation.h>
namespace hyperlight {
bool isLowPowerMode()
{
    return [[NSProcessInfo processInfo] isLowPowerModeEnabled];
}
} // namespace hyperlight
