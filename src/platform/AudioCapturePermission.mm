#include "platform/AudioCapturePermission.h"

#import <AppKit/AppKit.h>

namespace hyperwave::AudioCapturePermission {

void openSystemSettings()
{
    // Verified against SecurityPrivacyExtension on macOS 26: the anchor sits
    // alongside Privacy_Microphone and Privacy_ScreenCapture, and the pane it
    // opens is headed "System Audio Recording Only".
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security"
                                      @"?Privacy_AudioCapture"];
    if (![[NSWorkspace sharedWorkspace] openURL:url]) {
        // If the anchor is ever renamed, land on the Privacy pane rather than
        // doing nothing at all.
        [[NSWorkspace sharedWorkspace]
            openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security"]];
    }
}

void notifyOnNextActivation(void (*callback)(void *), void *context)
{
    if (!callback)
        return;

    // A grant made in System Settings produces no notification on our side, so
    // the app coming back to the foreground is the only cue we get that
    // something might have changed.
    __block id token = nil;
    token = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationDidBecomeActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *) {
                    callback(context);
                    if (token) {
                        [[NSNotificationCenter defaultCenter] removeObserver:token];
                        token = nil;
                    }
                }];
}

} // namespace hyperwave::AudioCapturePermission
