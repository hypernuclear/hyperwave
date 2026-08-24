#include "platform/MacNotchPresenter.h"

#include <algorithm>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <QDebug>

namespace {

// Geometry of the camera housing on the screen that owns the menu bar.
struct NotchGeometry
{
    bool valid = false;
    NSScreen *screen = nil;
    CGFloat notchWidth = 0;
    CGFloat notchHeight = 0;
    CGFloat menuBarHeight = 0;
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
    geometry.menuBarHeight = screen.frame.size.height - screen.visibleFrame.size.height;

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
        geometry.menuBarHeight = std::max<CGFloat>(geometry.menuBarHeight, 33.0);
        geometry.valid = true;
    }
    return geometry;
}

// Collapsed: how far the tab hangs below the housing.
constexpr CGFloat kTabHeight = 20.0;
// Expanded: the panel's size, and how far it overhangs the notch on each side.
constexpr CGFloat kExpandedHeight = 168.0;
constexpr CGFloat kExpandedOverhang = 96.0;
constexpr CGFloat kCornerRadius = 14.0;

} // namespace

// ---------------------------------------------------------------------------
// The panel's content view. Draws the black shape; the strip is a sublayer.
// ---------------------------------------------------------------------------
@interface HLNotchView : NSView
@property (nonatomic, assign) hyperlight::MacNotchPresenter *presenter;
@property (nonatomic, strong) CALayer *stripLayer;
@property (nonatomic, assign) CGFloat notchHeight;
@property (nonatomic, assign) CGFloat notchWidth;
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
        [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (void)mouseEntered:(NSEvent *)event
{
    (void)event;
    if (_presenter)
        _presenter->handleHover(true);
}

- (void)mouseExited:(NSEvent *)event
{
    (void)event;
    if (_presenter)
        _presenter->handleHover(false);
}

- (void)mouseDown:(NSEvent *)event
{
    (void)event;
    if (_presenter)
        _presenter->emitClicked();
}

@end

// A target for the display link, since a C++ object cannot be one.
@interface HLNotchHook : NSObject
@property (nonatomic, assign) hyperlight::MacNotchPresenter *presenter;
- (void)step:(id)sender;
- (void)screenParametersChanged:(NSNotification *)note;
@end

@implementation HLNotchHook

- (void)step:(id)sender
{
    (void)sender;
    if (_presenter)
        _presenter->emitFrameDue();
}

- (void)screenParametersChanged:(NSNotification *)note
{
    (void)note;
    if (_presenter)
        _presenter->emitGeometryChanged();
}

@end

namespace hyperlight {

struct MacNotchPresenter::Impl
{
    NSWindow *window = nil;
    HLNotchView *view = nil;
    HLNotchHook *hook = nil;
    CADisplayLink *link = nil;
    CGColorSpaceRef colorSpace = nullptr;

    NotchGeometry geometry;
    bool expanded = false;
    int preferredFps = 60;
    bool clockRunning = false;

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
    : BarPresenter(parent)
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
    m_impl->view.notchHeight = g.notchHeight;
    m_impl->view.notchWidth = g.notchWidth;
    m_impl->window.contentView = m_impl->view;

    [m_impl->window orderFrontRegardless];

    [[NSNotificationCenter defaultCenter]
        addObserver:m_impl->hook
           selector:@selector(screenParametersChanged:)
               name:NSApplicationDidChangeScreenParametersNotification
             object:nil];

    qInfo("hyperlight: notch panel installed (%.0f x %.0f pt notch, menu bar %.0f pt)",
          g.notchWidth, g.notchHeight, g.menuBarHeight);
    return true;
}

void MacNotchPresenter::remove()
{
    stopClock();
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
    return m_impl->expanded ? kExpandedHeight - m_impl->geometry.notchHeight : kTabHeight;
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
    m_impl->view.stripLayer.frame =
        CGRectMake((bounds.size.width - w) / 2.0, (kTabHeight - h) / 2.0, w, h);
    m_impl->view.stripLayer.contents = (__bridge id)cg;
    [CATransaction commit];

    CGImageRelease(cg);
}

bool MacNotchPresenter::isVisibleOnScreen() const
{
    // The window is ours and always ordered front; there is no overflow to be
    // pushed into and no other app's items to be crowded out by. That is the
    // entire point of drawing here.
    return m_impl->window != nil;
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

    const NotchGeometry &g = m_impl->geometry;
    const CGFloat width =
        expanded ? g.notchWidth + 2 * kExpandedOverhang : g.notchWidth;
    const CGFloat height = expanded ? kExpandedHeight : g.notchHeight + kTabHeight;
    const NSRect frame = NSMakeRect(NSMidX(g.screenFrame) - width / 2.0,
                                    NSMaxY(g.screenFrame) - height, width, height);

    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.22;
        context.timingFunction =
            [CAMediaTimingFunction functionWithControlPoints:0.2f:0.9f:0.2f:1.0f];
        [m_impl->window.animator setFrame:frame display:YES];
    }];

    Q_EMIT geometryChanged();
}

void MacNotchPresenter::handleHover(bool inside)
{
    setExpanded(inside);
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

void MacNotchPresenter::setMenu(const MenuModel &)
{
    // The panel has no menu of its own; the status item carries it. Expanding
    // is what this surface does instead.
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

} // namespace hyperlight
