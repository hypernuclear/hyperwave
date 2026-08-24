#pragma once

#ifdef __OBJC__
#import <AppKit/AppKit.h>
#endif

#include "app/MenuModel.h"

namespace hyperlight {

#ifdef __OBJC__

// Turns a MenuModel into an NSMenu.
//
// Shared because both surfaces need the same menu. The status item shows it on
// click; the notch panel shows it on right-click, and it has to, because the
// panel exists precisely for the case where the status item has been crowded
// off the bar and cannot be clicked at all.
//
// `target` receives `action` with the chosen NSMenuItem, whose representedObject
// is the MenuItem's actionId as an NSString.
NSMenu *buildNSMenu(const MenuModel &model, id target, SEL action, id<NSMenuDelegate> delegate);

// The app icon for a bundle id at menu size, or nil if it is not running.
NSImage *appIconForBundle(const QString &bundleId, CGFloat size);

#endif

} // namespace hyperlight
