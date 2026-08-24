#include "platform/MacMenuBuilder.h"

namespace hyperlight {

NSImage *appIconForBundle(const QString &bundleId, CGFloat size)
{
    if (bundleId.isEmpty())
        return nil;
    NSString *wanted = bundleId.toNSString();
    for (NSRunningApplication *app in [[NSWorkspace sharedWorkspace] runningApplications]) {
        if ([app.bundleIdentifier isEqualToString:wanted] && app.icon) {
            NSImage *icon = [app.icon copy];
            icon.size = NSMakeSize(size, size);
            return icon;
        }
    }
    return nil;
}

namespace {

void buildInto(NSMenu *menu, const MenuModel &model, id target, SEL action,
               id<NSMenuDelegate> delegate)
{
    for (const MenuItem &item : model) {
        if (item.kind == MenuItem::Kind::Separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }

        NSMenuItem *entry = [[NSMenuItem alloc] initWithTitle:item.title.toNSString()
                                                       action:nil
                                                keyEquivalent:@""];

        if (item.kind == MenuItem::Kind::Submenu) {
            NSMenu *child = [[NSMenu alloc] initWithTitle:item.title.toNSString()];
            child.autoenablesItems = NO;
            child.delegate = delegate;
            buildInto(child, item.children, target, action, delegate);
            entry.submenu = child;
        } else {
            entry.target = target;
            entry.action = action;
            entry.representedObject = item.actionId.toNSString();
            entry.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
        }

        entry.enabled = item.enabled;
        if (NSImage *icon = appIconForBundle(item.iconBundleId, 16.0))
            entry.image = icon;

        // The dimmed trailing text ("playing") rides along as an attributed
        // title, since NSMenuItem has no second label of its own.
        if (!item.detail.isEmpty()) {
            NSMutableAttributedString *title =
                [[NSMutableAttributedString alloc] initWithString:item.title.toNSString()];
            NSAttributedString *detail = [[NSAttributedString alloc]
                initWithString:[NSString stringWithFormat:@"   %@", item.detail.toNSString()]
                    attributes:@{
                        NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
                        NSFontAttributeName : [NSFont menuFontOfSize:[NSFont smallSystemFontSize]],
                    }];
            [title appendAttributedString:detail];
            entry.attributedTitle = title;
        }

        [menu addItem:entry];
    }
}

} // namespace

NSMenu *buildNSMenu(const MenuModel &model, id target, SEL action, id<NSMenuDelegate> delegate)
{
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"hyperlight"];
    menu.autoenablesItems = NO;
    menu.delegate = delegate;
    buildInto(menu, model, target, action, delegate);
    return menu;
}

} // namespace hyperlight
