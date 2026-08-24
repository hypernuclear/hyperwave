#include "app/Shell.h"

#import <AppKit/AppKit.h>

namespace hyperlight {

bool isLowPowerMode()
{
    return [[NSProcessInfo processInfo] isLowPowerModeEnabled];
}

void activateApp()
{
    [NSApp activateIgnoringOtherApps:YES];
}

void relaunchApp()
{
    NSURL *bundle = [[NSBundle mainBundle] bundleURL];

    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    // createsNewApplicationInstance, because the old instance is still alive at
    // this moment -- without it the open would simply activate us and nothing
    // would restart.
    config.createsNewApplicationInstance = YES;
    config.activates = YES;

    [[NSWorkspace sharedWorkspace]
        openApplicationAtURL:bundle
               configuration:config
           completionHandler:^(NSRunningApplication *, NSError *error) {
               if (error)
                   NSLog(@"hyperlight: relaunch failed: %@", error);
               // Terminate from the main queue whatever thread this lands on.
               dispatch_async(dispatch_get_main_queue(), ^{
                   [NSApp terminate:nil];
               });
           }];
}

} // namespace hyperlight
