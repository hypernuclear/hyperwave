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
constexpr CGFloat kExpandedSourcesHeight = 22.0;
constexpr CGFloat kExpandedPad = 10.0;
constexpr CGFloat kCornerRadius = 14.0;
CGFloat expandedHeight(CGFloat notchHeight)
{
    return notchHeight + kExpandedPad + kExpandedSourcesHeight + kExpandedPad
        + kExpandedStripHeight + kExpandedPad;
}

} // namespace

// ---------------------------------------------------------------------------
// The panel's content view. Draws the black shape; the strip is a sublayer.
// ---------------------------------------------------------------------------
@interface HLNotchView : NSView
@property (nonatomic, assign) hyperwave::MacNotchPresenter *presenter;
@property (nonatomic, strong) CALayer *stripLayer;
@property (nonatomic, strong) NSPopUpButton *sources;
@property (nonatomic, assign) CGFloat notchHeight;
@property (nonatomic, assign) CGFloat notchWidth;
@property (nonatomic, assign) BOOL expanded;
- (void)layoutContent;
- (void)setDevices:(const hyperwave::AudioDeviceList &)devices current:(const QString &)currentUid;
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

        _sources = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        _sources.bezelStyle = NSBezelStyleRoundRect;
        _sources.controlSize = NSControlSizeSmall;
        _sources.font = [NSFont systemFontOfSize:11.0];
        _sources.target = self;
        _sources.action = @selector(sourcePicked:);
        _sources.hidden = YES;
        [self addSubview:_sources];
    }
    return self;
}

// Laid out by hand rather than with constraints. The panel has two elements
// and two states; a constraint graph would be more machinery than arithmetic.
- (void)layoutContent
{
    const NSRect b = self.bounds;
    const BOOL show = _expanded;

    _sources.hidden = !show;

    if (!show)
        return;

    const CGFloat pad = 14.0;

    // Under the housing: the source picker, then the strip in the bottom row
    // expandedHeight() has already reserved for it -- nothing here may reach
    // into that row.
    //
    // The popup is given a fixed generous width rather than its fitting size,
    // so the panel does not change shape every time the selected device's name
    // changes length.
    const CGFloat sourcesWidth = std::min<CGFloat>(b.size.width - 2 * pad, 240.0);
    const CGFloat y = b.size.height - _notchHeight - kExpandedPad - kExpandedSourcesHeight;
    _sources.frame = NSMakeRect((b.size.width - sourcesWidth) / 2.0, y, sourcesWidth,
                                kExpandedSourcesHeight);
}

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self layoutContent];
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

- (void)rightMouseDown:(NSEvent *)event
{
    (void)event;
    // The panel's route to the menu, and it has to have one: this surface
    // exists for displays where the status item has been crowded off the bar
    // entirely, so "click the status item" is not an answer here.
    if (_presenter)
        _presenter->showMenu();
}


// Fills the source popup.
//
// A popup rather than a row of buttons: with ten output devices on this
// machine the row was wider than the panel, and a device is a name rather than
// an icon anyway -- "Scarlett Solo USB" does not have a face.
- (void)setDevices:(const hyperwave::AudioDeviceList &)devices
           current:(const QString &)currentUid
{
    [_sources removeAllItems];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Source"];
    NSMenuItem *all = [[NSMenuItem alloc] initWithTitle:@"All system audio"
                                                 action:nil
                                          keyEquivalent:@""];
    all.representedObject = @"";
    [menu addItem:all];
    if (!devices.isEmpty())
        [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *selected = all;
    for (const hyperwave::AudioDevice &device : devices) {
        NSString *title = device.isDefaultOutput
            ? [NSString stringWithFormat:@"%@  ·  output", device.name.toNSString()]
            : device.name.toNSString();
        NSMenuItem *entry = [[NSMenuItem alloc] initWithTitle:title
                                                       action:nil
                                                keyEquivalent:@""];
        entry.representedObject = device.uid.toNSString();
        [menu addItem:entry];
        if (device.uid == currentUid)
            selected = entry;
    }
    _sources.menu = menu;
    [_sources selectItem:selected];
}
- (void)sourcePicked:(NSPopUpButton *)sender
{
    id represented = sender.selectedItem.representedObject;
    if (_presenter && [represented isKindOfClass:[NSString class]])
        _presenter->handleSourcePick(QString::fromNSString(represented));
}
@end

// A target for the display link, since a C++ object cannot be one.
@interface HLNotchHook : NSObject
@property (nonatomic, assign) hyperwave::MacNotchPresenter *presenter;
- (void)step:(id)sender;
- (void)menuItemChosen:(NSMenuItem *)sender;
- (void)screenParametersChanged:(NSNotification *)note;
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
    m_impl->view.notchHeight = g.notchHeight;
    m_impl->view.notchWidth = g.notchWidth;
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
    // Collapsed the strip owns the tab; expanded it sits in the bottom row,
    // clear of the source picker above it.
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
    m_impl->view.expanded = expanded;
    applyFrame();
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

void MacNotchPresenter::applyFrame()
{
    if (!m_impl->window)
        return;

    const NotchGeometry &g = m_impl->geometry;
    CGFloat width = g.notchWidth;
    CGFloat height = g.notchHeight;

    if (m_impl->expanded) {
        width = g.notchWidth + 2 * kExpandedOverhang;
        height = expandedHeight(g.notchHeight);
    } else if (m_impl->tabVisible) {
        height = g.notchHeight + kTabHeight;
    }
    // Otherwise: exactly the housing, which is invisible against the housing.
    // The window stays alive so the notch remains hoverable -- retracting it
    // entirely would leave no way in on a display crowded enough to have hidden
    // the status item, which is the situation this panel exists for.

    // Retracted, the window occupies exactly the housing, which is a physical
    // cutout -- nothing drawn there reaches the panel. Hiding the strip anyway
    // matters for two reasons: a screen recording DOES capture the notch
    // region, and there is no point compositing bars nobody can see.
    const bool showStrip = m_impl->expanded || m_impl->tabVisible;
    m_impl->view.stripLayer.hidden = !showStrip;

    const NSRect frame = NSMakeRect(NSMidX(g.screenFrame) - width / 2.0,
                                    NSMaxY(g.screenFrame) - height, width, height);
    [m_impl->window setFrame:frame display:YES animate:NO];
    [m_impl->view setFrameSize:frame.size];
    [m_impl->view layoutContent];
    Q_EMIT geometryChanged();
}

void MacNotchPresenter::handleSourcePick(const QString &bundleId)
{
    Q_EMIT menuActionTriggered(QLatin1String(MenuAction::kSourcePrefix) + bundleId);
}

void MacNotchPresenter::setDevices(const AudioDeviceList &devices, const QString &current)
{
    if (m_impl->view)
        [m_impl->view setDevices:devices current:current];
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
