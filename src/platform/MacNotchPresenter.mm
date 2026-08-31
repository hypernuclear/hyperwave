#include "platform/MacNotchPresenter.h"

#include <algorithm>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <QDebug>

#include "audio/AudioProcess.h"
#include "platform/MacMenuBuilder.h"

namespace {

// Geometry of the camera housing on the screen that owns the menu bar.
struct NotchGeometry
{
    bool valid = false;
    NSScreen *screen = nil;
    CGFloat notchWidth = 0;
    CGFloat notchHeight = 0;
    // frame minus visibleFrame, so this is menu bar PLUS Dock, not the menu
    // bar alone. Only used as a floor for the faked geometry and in logging;
    // nothing lays anything out with it.
    CGFloat screenChromeHeight = 0;
    NSRect screenFrame = NSZeroRect;
};

bool g_fakeNotch = false;

NotchGeometry notchGeometry()
{
    NotchGeometry geometry;

    // The menu bar lives on screens[0]; mainScreen follows the key window,
    // which for a background agent is nobody.
    NSScreen *screen = [NSScreen screens].firstObject;
    if (!screen)
        return geometry;

    geometry.screen = screen;
    geometry.screenFrame = screen.frame;
    geometry.screenChromeHeight = screen.frame.size.height - screen.visibleFrame.size.height;

    if (@available(macOS 12.0, *)) {
        const CGFloat inset = screen.safeAreaInsets.top;
        const NSRect left = screen.auxiliaryTopLeftArea;
        const NSRect right = screen.auxiliaryTopRightArea;

        if (inset > 0 && left.size.width > 0 && right.size.width > 0) {
            // The notch is the gap the two unobscured areas leave between them.
            geometry.notchWidth = NSMinX(right) - NSMaxX(left);
            geometry.notchHeight = inset;
            geometry.valid = geometry.notchWidth > 0;
            return geometry;
        }
    }

    if (g_fakeNotch) {
        // A 14-inch MacBook Pro, for developing on an external monitor.
        geometry.notchWidth = 185.0;
        geometry.notchHeight = 32.0;
        geometry.screenChromeHeight = std::max<CGFloat>(geometry.screenChromeHeight, 33.0);
        geometry.valid = true;
    }
    return geometry;
}

// Collapsed: how far the tab hangs below the housing.
constexpr CGFloat kTabHeight = 20.0;
// Expanded: how far the panel overhangs the notch on each side, and the
// vertical budget. The height is a sum rather than a round number so that
// changing any one row does not silently start overlapping the next.
constexpr CGFloat kExpandedOverhang = 96.0;
constexpr CGFloat kExpandedStripHeight = 54.0;
constexpr CGFloat kExpandedPad = 10.0;
constexpr CGFloat kCornerRadius = 14.0;

CGFloat expandedHeight(CGFloat notchHeight)
{
    return notchHeight + kExpandedPad + kExpandedStripHeight + kExpandedPad;
}

// Hover. Expanding is given longer than collapsing: the panel arriving is what
// the pointer is waiting for, while the panel leaving should get out of the way.
// The curve is weighted to the finish, so the size settles rather than stopping.
constexpr NSTimeInterval kExpandSeconds = 0.22;
constexpr NSTimeInterval kCollapseSeconds = 0.16;

CAMediaTimingFunction *hoverCurve()
{
    return [CAMediaTimingFunction functionWithControlPoints:0.2f:0.0f:0.0f:1.0f];
}

// How often the panel checks that the pointer is still on it while open. See
// startHoverWatch() for why it cannot rely on being told.
constexpr NSTimeInterval kHoverWatchSeconds = 0.15;

// Where the window sits for a given state. Derived from the notch rather than
// read back from the window, so a frame that is halfway through an animation
// cannot change the answer to "is the pointer on the panel".
NSRect panelFrame(const NotchGeometry &g, bool expanded, bool tabVisible)
{
    CGFloat width = g.notchWidth;
    CGFloat height = g.notchHeight;

    if (expanded) {
        width = g.notchWidth + 2 * kExpandedOverhang;
        height = expandedHeight(g.notchHeight);
    } else if (tabVisible) {
        height = g.notchHeight + kTabHeight;
    }
    // Otherwise: exactly the housing, which is invisible against the housing.

    return NSMakeRect(NSMidX(g.screenFrame) - width / 2.0, NSMaxY(g.screenFrame) - height, width,
                      height);
}

} // namespace

// ---------------------------------------------------------------------------
// The panel's content view. Draws the black shape; the strip is a sublayer.
// ---------------------------------------------------------------------------
@interface HLNotchView : NSView
@property (nonatomic, assign) hyperwave::MacNotchPresenter *presenter;
@property (nonatomic, strong) CALayer *stripLayer;
@end

@implementation HLNotchView {
    NSTrackingArea *_tracking;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = NSColor.blackColor.CGColor;
        self.layer.masksToBounds = YES;

        // Only the bottom corners are rounded. The top edge runs flush into the
        // real housing, which is what makes the tab read as part of it rather
        // than as a window that happens to be parked there.
        self.layer.cornerRadius = kCornerRadius;
        self.layer.maskedCorners = kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner;

        _stripLayer = [CALayer layer];
        _stripLayer.contentsGravity = kCAGravityCenter;
        _stripLayer.masksToBounds = YES;
        [self.layer addSublayer:_stripLayer];
    }
    return self;
}

// No subview layout to do. The panel is one black shape with the strip as a
// sublayer, and present() places that from the view's live bounds -- which is
// what lets the strip track a window frame that is mid-animation.
- (BOOL)isFlipped { return NO; }
// Deliberately NOT wantsUpdateLayer. With it set and no -updateLayer of our
// own, AppKit takes over the backing layer and quietly discards the corner
// radius and mask configured in init.
- (BOOL)wantsUpdateLayer { return NO; }
- (void)drawRect:(NSRect)dirty { (void)dirty; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_tracking)
        return;   // InVisibleRect areas follow the view; they never need rebuilding

    // NSTrackingInVisibleRect, and created exactly once.
    //
    // The obvious implementation rebuilds the area from self.bounds on every
    // updateTrackingAreas. Do not: removing a tracking area while the pointer
    // is inside it synthesises a mouseExited, and since expanding the panel
    // resizes the view, that exit collapses it again immediately -- the panel
    // flickers between states and settles closed. An InVisibleRect area tracks
    // the view's geometry itself and survives the resize.
    _tracking = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways
                     | NSTrackingInVisibleRect
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
}

// Both of these are a nudge to go and look, never the answer.
//
// Animating the window's size makes AppKit re-evaluate tracking on every step
// of the animation, and it emits a full exit/enter pair each time even though
// the pointer has not moved. Measured with the pointer parked on the notch:
// 109 events in four seconds, every one of them reporting a location inside
// the window. Acting on them directly restarted the animation on each pair, so
// the panel never reached its open size and flickered between the two.
- (void)mouseEntered:(NSEvent *)event
{
    (void)event;
    if (_presenter)
        _presenter->reviewHover();
}

- (void)mouseExited:(NSEvent *)event
{
    (void)event;
    if (_presenter)
        _presenter->reviewHover();
}

- (void)mouseDown:(NSEvent *)event
{
    (void)event;
    if (_presenter)
        _presenter->emitClicked();
}

- (void)rightMouseDown:(NSEvent *)event
{
    (void)event;
    // The panel's route to the menu, and it has to have one: this surface
    // exists for displays where the status item has been crowded off the bar
    // entirely, so "click the status item" is not an answer here.
    if (_presenter)
        _presenter->showMenu();
}

@end

// A target for the display link, since a C++ object cannot be one.
@interface HLNotchHook : NSObject
@property (nonatomic, assign) hyperwave::MacNotchPresenter *presenter;
- (void)step:(id)sender;
- (void)menuItemChosen:(NSMenuItem *)sender;
- (void)screenParametersChanged:(NSNotification *)note;
- (void)hoverWatchFired:(NSTimer *)timer;
@end

@implementation HLNotchHook

- (void)step:(id)sender
{
    (void)sender;
    if (_presenter)
        _presenter->emitFrameDue();
}

- (void)menuItemChosen:(NSMenuItem *)sender
{
    // Declared but never implemented, while the panel's menu wired every item
    // to it. Choosing anything in that menu raised an unrecognised selector and
    // took the app down -- and that menu is not a convenience, it is the only
    // way in on a display where the status item has been crowded under the
    // notch. The compiler had been saying so the whole time, as
    // -Wincomplete-implementation.
    if (_presenter && [sender.representedObject isKindOfClass:[NSString class]])
        _presenter->emitMenuAction(QString::fromNSString(sender.representedObject));
}

- (void)screenParametersChanged:(NSNotification *)note
{
    (void)note;
    if (_presenter)
        _presenter->emitGeometryChanged();
}

- (void)hoverWatchFired:(NSTimer *)timer
{
    (void)timer;
    if (_presenter)
        _presenter->reviewHover();
}

@end

namespace hyperwave {

struct MacNotchPresenter::Impl
{
    NSWindow *window = nil;
    HLNotchView *view = nil;
    HLNotchHook *hook = nil;
    CADisplayLink *link = nil;
    CGColorSpaceRef colorSpace = nullptr;

    NSMenu *menu = nil;
    NotchGeometry geometry;
    bool expanded = false;
    bool tabVisible = true;
    int preferredFps = 60;
    bool clockRunning = false;
    // Runs only while the panel is open. See startHoverWatch().
    NSTimer *hoverWatch = nil;
    // Ordered out because the housing went away under us. Tracked rather than
    // re-ordered every frame: ordering a window front on every geometry change
    // fights with whatever else is at NSStatusWindowLevel.
    bool offScreen = false;

    // Two, for the same reason the status item needs two: Core Animation reads
    // the bytes after present() returns.
    QImage buffers[2];
    int nextBuffer = 0;
};

bool MacNotchPresenter::isAvailable()
{
    return notchGeometry().valid;
}

void MacNotchPresenter::setFakeNotch(bool enabled)
{
    g_fakeNotch = enabled;
}

MacNotchPresenter::MacNotchPresenter(QObject *parent)
    : PanelPresenter(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}

MacNotchPresenter::~MacNotchPresenter()
{
    remove();
    if (m_impl->colorSpace) {
        CGColorSpaceRelease(m_impl->colorSpace);
        m_impl->colorSpace = nullptr;
    }
}

bool MacNotchPresenter::install()
{
    if (m_impl->window)
        return true;

    m_impl->geometry = notchGeometry();
    if (!m_impl->geometry.valid)
        return false;

    const NotchGeometry &g = m_impl->geometry;
    const CGFloat width = g.notchWidth;
    const CGFloat height = g.notchHeight + kTabHeight;

    // Centred on the notch, top edge flush with the top of the screen.
    const NSRect frame =
        NSMakeRect(NSMidX(g.screenFrame) - width / 2.0,
                   NSMaxY(g.screenFrame) - height, width, height);

    m_impl->window = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    m_impl->window.opaque = NO;
    m_impl->window.backgroundColor = NSColor.clearColor;
    m_impl->window.hasShadow = NO;
    m_impl->window.ignoresMouseEvents = NO;

    // Above the menu bar (24) so the tab is not clipped by it, but below the
    // menus themselves so an open menu still wins.
    m_impl->window.level = NSStatusWindowLevel;

    // Follow the user between spaces and survive another app going fullscreen;
    // stationary keeps Mission Control from sliding it around.
    m_impl->window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorStationary
        | NSWindowCollectionBehaviorIgnoresCycle;

    m_impl->hook = [[HLNotchHook alloc] init];
    m_impl->hook.presenter = this;

    m_impl->view = [[HLNotchView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    m_impl->view.presenter = this;
    m_impl->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    m_impl->window.contentView = m_impl->view;

    [m_impl->window orderFrontRegardless];

    [[NSNotificationCenter defaultCenter]
        addObserver:m_impl->hook
           selector:@selector(screenParametersChanged:)
               name:NSApplicationDidChangeScreenParametersNotification
             object:nil];

    qInfo("hyperwave: notch panel installed (%.0f x %.0f pt notch)", g.notchWidth,
          g.notchHeight);
    return true;
}

void MacNotchPresenter::remove()
{
    stopClock();
    stopHoverWatch();
    if (m_impl->hook) {
        [[NSNotificationCenter defaultCenter] removeObserver:m_impl->hook];
        m_impl->hook.presenter = nullptr;
        m_impl->hook = nil;
    }
    if (m_impl->view) {
        m_impl->view.presenter = nullptr;
        m_impl->view = nil;
    }
    if (m_impl->window) {
        [m_impl->window orderOut:nil];
        m_impl->window = nil;
    }
}

qreal MacNotchPresenter::heightPt() const
{
    // Only the tab is drawable; the rest of the window sits behind the housing.
    // Only the strip's own row, not everything below the housing -- the source
    // row has already claimed its own.
    return m_impl->expanded ? kExpandedStripHeight : kTabHeight;
}

qreal MacNotchPresenter::availableWidthPt() const
{
    const CGFloat width = m_impl->expanded
        ? m_impl->geometry.notchWidth + 2 * kExpandedOverhang
        : m_impl->geometry.notchWidth;
    // Keep clear of the rounded corners.
    return std::max<CGFloat>(width - 2 * kCornerRadius, 16.0);
}

qreal MacNotchPresenter::devicePixelRatio() const
{
    if (m_impl->window)
        return m_impl->window.backingScaleFactor;
    return m_impl->geometry.screen ? m_impl->geometry.screen.backingScaleFactor : 2.0;
}

void MacNotchPresenter::setWidthPt(qreal)
{
    // Ignored on purpose. A status item grows to fit its content; this panel is
    // the size of the hole in the display, and the strip fits itself to that.
}

void MacNotchPresenter::present(const QImage &image)
{

    if (!m_impl->view || image.isNull())
        return;

    QImage &slot = m_impl->buffers[m_impl->nextBuffer];
    slot = image;
    m_impl->nextBuffer = 1 - m_impl->nextBuffer;

    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, slot.constBits(), static_cast<std::size_t>(slot.sizeInBytes()), nullptr);
    if (!provider)
        return;

    CGImageRef cg = CGImageCreate(
        static_cast<std::size_t>(slot.width()), static_cast<std::size_t>(slot.height()), 8, 32,
        static_cast<std::size_t>(slot.bytesPerLine()), m_impl->colorSpace,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst)
            | static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little),
        provider, nullptr, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    if (!cg)
        return;

    const CGFloat dpr = devicePixelRatio();
    const CGFloat w = slot.width() / dpr;
    const CGFloat h = slot.height() / dpr;
    const NSRect bounds = m_impl->view.bounds;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    m_impl->view.stripLayer.contentsScale = dpr;
    // Bottom-anchored: the strip lives in the tab, below the housing.
    // Collapsed the strip owns the tab; expanded it sits in the bottom row
    // expandedHeight() reserves for it, one pad clear of the housing above.
    //
    // Read from the view's bounds every frame, which is what carries the strip
    // along with a window frame that is mid-animation.
    const CGFloat y = m_impl->expanded ? kExpandedPad : (kTabHeight - h) / 2.0;
    m_impl->view.stripLayer.frame = CGRectMake((bounds.size.width - w) / 2.0, y, w, h);
    m_impl->view.stripLayer.contents = (__bridge id)cg;
    [CATransaction commit];

    CGImageRelease(cg);
}

bool MacNotchPresenter::isVisibleOnScreen() const
{
    // The window is ours and always ordered front; there is no overflow to be
    // pushed into and no other app's items to be crowded out by. That is the
    // entire point of drawing here.
    //
    // The geometry check is the exception: between the lid closing and App
    // dropping the surface, the window is still alive but there is no housing
    // to draw under, and frames spent on it go nowhere visible.
    return m_impl->window != nil && m_impl->geometry.valid;
}

bool MacNotchPresenter::isExpanded() const
{
    return m_impl->expanded;
}

void MacNotchPresenter::setExpanded(bool expanded)
{
    if (expanded == m_impl->expanded || !m_impl->window)
        return;
    m_impl->expanded = expanded;
    // The one animated transition. Everything else that moves this window --
    // the tab retracting, a display being reconfigured -- is either a state
    // change nobody is looking at or a jump to somewhere else entirely, and
    // easing those only makes them look uncertain.
    applyFrame(true);

    if (expanded)
        startHoverWatch();
    else
        stopHoverWatch();
}

void MacNotchPresenter::reviewHover()
{
    if (!m_impl->window || !m_impl->geometry.valid)
        return;

    // Hysteresis, and the reason this asks rather than being told: the tab is
    // what you aim at to open the panel, and the whole panel is what you have
    // to leave to close it again. Both rectangles come from the notch geometry,
    // so an animation in progress cannot change the answer -- which is what
    // stops AppKit's resize-driven exit/enter pairs from reopening the question
    // sixty times a second.
    const NSRect target = panelFrame(m_impl->geometry, m_impl->expanded, m_impl->tabVisible);
    setExpanded(NSPointInRect([NSEvent mouseLocation], target));
}

void MacNotchPresenter::startHoverWatch()
{
    if (m_impl->hoverWatch || !m_impl->hook)
        return;

    // The backstop for a departure nobody reports.
    //
    // Because the exits AppKit sends mid-animation are indistinguishable from
    // real ones, its idea of inside-ness ends up inverted from ours, and then
    // the exit for the pointer actually leaving never arrives: the panel would
    // stay open until something else disturbed it. Polling closes that hole,
    // and it only runs while the panel is open, so an idle notch still costs
    // nothing.
    m_impl->hoverWatch = [NSTimer timerWithTimeInterval:kHoverWatchSeconds
                                                 target:m_impl->hook
                                               selector:@selector(hoverWatchFired:)
                                               userInfo:nil
                                                repeats:YES];
    // Common modes, for the same reason the display link uses them: the panel
    // must not freeze open while its own right-click menu is tracking.
    [[NSRunLoop mainRunLoop] addTimer:m_impl->hoverWatch forMode:NSRunLoopCommonModes];
}

void MacNotchPresenter::stopHoverWatch()
{
    [m_impl->hoverWatch invalidate];
    m_impl->hoverWatch = nil;
}

void MacNotchPresenter::setPreferredFps(int fps)
{
    fps = std::clamp(fps, 15, 120);
    if (fps == m_impl->preferredFps)
        return;
    m_impl->preferredFps = fps;
    if (m_impl->clockRunning) {
        stopClock();
        startClock();
    }
}

void MacNotchPresenter::startClock()
{
    if (m_impl->clockRunning || !m_impl->view || !m_impl->hook)
        return;
    if (@available(macOS 14.0, *)) {
        m_impl->link = [m_impl->view displayLinkWithTarget:m_impl->hook selector:@selector(step:)];
        const auto fps = static_cast<float>(m_impl->preferredFps);
        m_impl->link.preferredFrameRateRange = CAFrameRateRangeMake(fps, fps, fps);
        [m_impl->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        m_impl->clockRunning = m_impl->link != nil;
    }
}

void MacNotchPresenter::stopClock()
{
    if (m_impl->link) {
        [m_impl->link invalidate];
        m_impl->link = nil;
    }
    m_impl->clockRunning = false;
}

bool MacNotchPresenter::isClockRunning() const
{
    return m_impl->clockRunning;
}

void MacNotchPresenter::setMenu(const MenuModel &model)
{
    m_impl->menu = buildNSMenu(model, m_impl->hook, @selector(menuItemChosen:), nil);
}

void MacNotchPresenter::showMenu()
{
    if (!m_impl->view)
        return;
    // Ask for a fresh menu first, and only then look at what we have.
    //
    // This surface never did, which made its menu a snapshot of whenever the
    // status item's menu last opened -- stale check marks, a "Playing" list
    // from minutes ago, output devices that had since been unplugged. Worse on
    // exactly the display this panel exists for: if the status item has been
    // crowded under the housing then its menu never opens, nothing ever asks
    // for a rebuild, and the panel's menu stays as old as the launch.
    Q_EMIT menuAboutToShow();
    if (!m_impl->menu)
        return;

    // Below the panel rather than at the pointer, so it reads as belonging to
    // the panel rather than as a context menu that happened to open there.
    const NSRect b = m_impl->view.bounds;
    [m_impl->menu popUpMenuPositioningItem:nil
                                atLocation:NSMakePoint(b.size.width / 2.0, 0)
                                    inView:m_impl->view];
}

void MacNotchPresenter::setTabVisible(bool visible)
{
    if (visible == m_impl->tabVisible)
        return;
    m_impl->tabVisible = visible;

    // Expanded wins: the panel must not retract out from under a pointer that
    // is hovering it just because the music stopped.
    if (m_impl->expanded)
        return;

    applyFrame();
}

bool MacNotchPresenter::isTabVisible() const
{
    return m_impl->tabVisible;
}

void MacNotchPresenter::applyFrame(bool animated)
{
    if (!m_impl->window)
        return;

    const NotchGeometry &g = m_impl->geometry;

    // No housing on the screen that owns the menu bar: the lid was shut and an
    // external display took over. notchGeometry() zeroes the notch but still
    // fills in screenFrame, so laying out from it produced a zero-width,
    // tab-height window parked at the top centre of a display with no notch to
    // hide it -- ordered front, at NSStatusWindowLevel, being drawn into.
    //
    // App tears the surface down when it notices, but that is coalesced behind
    // a timer and this runs first, straight off the screen notification.
    if (!g.valid) {
        if (!m_impl->offScreen) {
            m_impl->view.stripLayer.hidden = YES;
            [m_impl->window orderOut:nil];
            m_impl->offScreen = true;
        }
        return;
    }
    if (m_impl->offScreen) {
        [m_impl->window orderFrontRegardless];
        m_impl->offScreen = false;
    }

    // Shared with reviewHover(), which needs the rectangle for a state the
    // window is not in yet. The window stays alive even when that rectangle is
    // exactly the housing, so the notch remains hoverable -- retracting it
    // entirely would leave no way in on a display crowded enough to have hidden
    // the status item, which is the situation this panel exists for.
    const NSRect frame = panelFrame(g, m_impl->expanded, m_impl->tabVisible);

    // Retracted, the window occupies exactly the housing, which is a physical
    // cutout -- nothing drawn there reaches the panel. Hiding the strip anyway
    // matters for two reasons: a screen recording DOES capture the notch
    // region, and there is no point compositing bars nobody can see.
    const bool showStrip = m_impl->expanded || m_impl->tabVisible;
    m_impl->view.stripLayer.hidden = !showStrip;

    // The window resizes its own content view, and present() places the strip
    // from that view's live bounds every frame, so the strip rides the
    // animation instead of having to be animated alongside it. Setting the
    // view's frame here by hand would put it at the final size immediately and
    // undo exactly that.
    if (animated) {
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
            context.duration = m_impl->expanded ? kExpandSeconds : kCollapseSeconds;
            context.timingFunction = hoverCurve();
            [m_impl->window.animator setFrame:frame display:YES];
        }];
    } else {
        [m_impl->window setFrame:frame display:YES animate:NO];
    }

    // Emitted with the final geometry rather than per animation step: this is
    // what sizes the render buffers, and reallocating them sixty times over a
    // fifth of a second to land back where one call would have put them is not
    // a smoother panel, only a busier one.
    Q_EMIT geometryChanged();
}

void MacNotchPresenter::setDevices(const AudioDeviceList &devices, const QString &current)
{
    // Nothing to fill. The panel used to carry a source popup in the expanded
    // state; it was a second copy of a list the right-click menu already has,
    // and it cost the panel a 32pt row that pushed the strip further from the
    // housing. The menu is one right-click away on the panel itself, which is
    // the same gesture that reaches it on the status item.
    //
    // The override stays because PanelPresenter declares it for the Windows
    // panel, which does have a picker.
    Q_UNUSED(devices);
    Q_UNUSED(current);
}

void MacNotchPresenter::emitFrameDue()
{
    Q_EMIT frameDue();
}

void MacNotchPresenter::emitGeometryChanged()
{
    m_impl->geometry = notchGeometry();
    Q_EMIT geometryChanged();
}

void MacNotchPresenter::emitClicked()
{
    Q_EMIT clicked();
}

void MacNotchPresenter::emitMenuAction(const QString &actionId)
{
    Q_EMIT menuActionTriggered(actionId);
}

} // namespace hyperwave
