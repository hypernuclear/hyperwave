#include "update/SparkleUpdater.h"

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

// Bridges Sparkle's KVO-based canCheckForUpdates to a Qt signal.
@interface HWUpdaterDelegate : NSObject
@property (nonatomic, strong) SPUStandardUpdaterController *controller;
@property (nonatomic, assign) hyperwave::SparkleUpdater *owner;
@end

@implementation HWUpdaterDelegate

- (void)startObserving
{
    [self.controller.updater
        addObserver:self
         forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))
            options:(NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew)
            context:nil];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context
{
    if ([keyPath isEqualToString:NSStringFromSelector(@selector(canCheckForUpdates))]) {
        if (self.owner)
            Q_EMIT self.owner->canCheckForUpdatesChanged(
                self.controller.updater.canCheckForUpdates);
        return;
    }
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}

- (void)dealloc
{
    [self.controller.updater
        removeObserver:self
            forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))];
}

@end

namespace hyperwave {

namespace {
inline HWUpdaterDelegate *del(void *p) { return (__bridge HWUpdaterDelegate *)p; }
} // namespace

SparkleUpdater::SparkleUpdater(QObject *parent)
    : AppUpdater(parent)
{
    @autoreleasepool {
        HWUpdaterDelegate *d = [[HWUpdaterDelegate alloc] init];
        d.owner = this;
        // startingUpdater:YES begins the background schedule immediately. Both
        // the feed URL and the EdDSA public key come from Info.plist, which is
        // also what Sparkle's installer reads -- keeping them in one place
        // means a feed move cannot leave the two disagreeing.
        d.controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                                     updaterDelegate:nil
                                                                  userDriverDelegate:nil];
        [d startObserving];
        m_delegate = (__bridge_retained void *)d;
    }
}

SparkleUpdater::~SparkleUpdater()
{
    @autoreleasepool {
        del(m_delegate).owner = nullptr;
        CFRelease(m_delegate);
        m_delegate = nullptr;
    }
}

bool SparkleUpdater::canCheckForUpdates() const
{
    @autoreleasepool {
        return del(m_delegate).controller.updater.canCheckForUpdates;
    }
}

void SparkleUpdater::checkForUpdates()
{
    @autoreleasepool {
        // hyperwave is LSUIElement, so it is not in the activation order and
        // Sparkle's window would open behind whatever the user was looking at
        // -- having just been asked for from a menu they clicked.
        [NSApp activateIgnoringOtherApps:YES];
        [del(m_delegate).controller checkForUpdates:nil];
    }
}

void SparkleUpdater::setAutomaticChecksEnabled(bool enabled)
{
    @autoreleasepool {
        del(m_delegate).controller.updater.automaticallyChecksForUpdates = enabled;
    }
}

AppUpdater *AppUpdater::create(QObject *parent)
{
    return new SparkleUpdater(parent);
}

} // namespace hyperwave
