#include "app/Shell.h"

#include <QWindow>

#import <AppKit/AppKit.h>

namespace hyperwave {

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
                   NSLog(@"hyperwave: relaunch failed: %@", error);
               // Terminate from the main queue whatever thread this lands on.
               dispatch_async(dispatch_get_main_queue(), ^{
                   [NSApp terminate:nil];
               });
           }];
}

void hideWindowTitle(QWindow *window)
{
    if (!window)
        return;
    window->create();
    // A bridged cast: this file is ARC, and the view is Qt's, not ours to own.
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (NSWindow *nsWindow = view.window)
        nsWindow.titleVisibility = NSWindowTitleHidden;
}

} // namespace hyperwave
