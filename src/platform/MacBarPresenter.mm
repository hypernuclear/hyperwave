#include "platform/MacBarPresenter.h"

#include <algorithm>
#include <cmath>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <QDebug>

#include "platform/MacMenuBuilder.h"

// The Objective-C classes have to sit at global scope -- the language has one
// flat class namespace and the compiler rejects @interface inside a C++
// namespace outright. Everything C++ goes back inside hyperlight below.

// ---------------------------------------------------------------------------
// The view. Deliberately as dumb as possible: it hosts a layer and never draws
// anything itself.
//
// wantsUpdateLayer plus RedrawNever means AppKit will not ask us to draw into a
// backing store; we hand Core Animation a CGImage per frame and it composites.
// That is both cheaper than -drawRect: and the only route that keeps a QPainter
// renderer viable at 60 fps.
// ---------------------------------------------------------------------------
@interface HLBarView : NSView
@property (nonatomic, assign) hyperlight::MacBarPresenter *presenter;
@end

@implementation HLBarView

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
        self.layer.contentsGravity = kCAGravityCenter;
        self.layer.masksToBounds = YES;
    }
    return self;
}

- (BOOL)wantsUpdateLayer { return YES; }

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    if (_presenter)
        _presenter->emitGeometryChanged();
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if (_presenter)
        _presenter->emitGeometryChanged();
}

@end

// A small Objective-C shim so the display link and the notification centre have
// something to target. The C++ object cannot be a selector target directly.
@interface HLPresenterHook : NSObject <NSMenuDelegate>
@property (nonatomic, assign) hyperlight::MacBarPresenter *presenter;
- (void)step:(id)sender;
- (void)statusItemClicked:(id)sender;
- (void)menuItemChosen:(NSMenuItem *)sender;
- (void)screenParametersChanged:(NSNotification *)note;
- (void)occlusionChanged:(NSNotification *)note;
@end

@implementation HLPresenterHook

- (void)step:(id)sender
{
    (void)sender;
    if (_presenter)
        _presenter->emitFrameDue();
}

- (void)statusItemClicked:(id)sender
{
    (void)sender;
    if (_presenter)
        _presenter->emitClicked();
}

- (void)menuItemChosen:(NSMenuItem *)sender
{
    if (_presenter && [sender.representedObject isKindOfClass:[NSString class]])
        _presenter->emitMenuAction(QString::fromNSString(sender.representedObject));
}

- (void)menuWillOpen:(NSMenu *)menu
{
    (void)menu;
    // The app list is live, so it is rebuilt at the moment of opening rather
    // than polled on a timer for a menu nobody is looking at.
    if (_presenter)
        _presenter->emitMenuAboutToShow();
}

- (void)screenParametersChanged:(NSNotification *)note
{
    (void)note;
    if (_presenter)
        _presenter->emitGeometryChanged();
}

- (void)occlusionChanged:(NSNotification *)note
{
    (void)note;
    if (_presenter)
        _presenter->emitGeometryChanged();
}

@end

namespace hyperlight {

namespace {

// macOS reports 24pt today. Hardcoding that would be a small bet with no
// upside, so it is queried and this is only the last resort.
constexpr CGFloat kFallbackThickness = 24.0;

} // namespace

struct MacBarPresenter::Impl
{
    NSStatusItem *statusItem = nil;
    HLBarView *view = nil;
    HLPresenterHook *hook = nil;
    CADisplayLink *link = nil;
    CGColorSpaceRef colorSpace = nullptr;
    NSMenu *menu = nil;

    // Two buffers, alternated.
    //
    // CGDataProviderCreateWithData does not copy, and Core Animation may still
    // be reading the bytes after present() returns. Reusing a single image
    // produces intermittent tearing under load that looks exactly like a
    // threading bug somewhere else entirely.
    QImage buffers[2];
    int nextBuffer = 0;

    qreal widthPt = 108.0;
    int preferredFps = 60;
    bool clockRunning = false;
};

MacBarPresenter::MacBarPresenter(QObject *parent)
    : BarPresenter(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}

MacBarPresenter::~MacBarPresenter()
{
    remove();
    if (m_impl->colorSpace) {
        CGColorSpaceRelease(m_impl->colorSpace);
        m_impl->colorSpace = nullptr;
    }
}

bool MacBarPresenter::install()
{
    if (m_impl->statusItem)
        return true;

    NSStatusBar *bar = [NSStatusBar systemStatusBar];
    const CGFloat thickness = bar.thickness > 0 ? bar.thickness : kFallbackThickness;

    m_impl->statusItem = [bar statusItemWithLength:m_impl->widthPt];
    if (!m_impl->statusItem)
        return false;

    // macOS remembers where the user dragged the item across launches, but only
    // if it has a name to remember it by.
    m_impl->statusItem.autosaveName = @"com.hypernuclear.hyperlight.status";

    m_impl->hook = [[HLPresenterHook alloc] init];
    m_impl->hook.presenter = this;

    m_impl->view = [[HLBarView alloc] initWithFrame:NSMakeRect(0, 0, m_impl->widthPt, thickness)];
    m_impl->view.presenter = this;
    m_impl->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSStatusBarButton *button = m_impl->statusItem.button;
    // Never assign button.image. An image here goes through AppKit's template
    // handling, which is the exact thing this whole class exists to avoid.
    button.image = nil;
    button.title = @"";
    // With neither a title nor an image the button has no accessible content
    // at all: VoiceOver has nothing to announce and the accessibility API
    // reports the item as a zero-sized rect, which also defeats scripting. The
    // label costs nothing and is the only thing standing in for the pixels.
    button.accessibilityLabel = @"hyperlight audio visualiser";
    button.toolTip = @"hyperlight";
    button.target = m_impl->hook;
    button.action = @selector(statusItemClicked:);
    [button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    [button addSubview:m_impl->view];

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:m_impl->hook
           selector:@selector(screenParametersChanged:)
               name:NSApplicationDidChangeScreenParametersNotification
             object:nil];
    [nc addObserver:m_impl->hook
           selector:@selector(occlusionChanged:)
               name:NSWindowDidChangeOcclusionStateNotification
             object:nil];

    return true;
}

void MacBarPresenter::remove()
{
    stopClock();

    if (m_impl->hook) {
        [[NSNotificationCenter defaultCenter] removeObserver:m_impl->hook];
        m_impl->hook.presenter = nullptr;
        m_impl->hook = nil;
    }
    if (m_impl->view) {
        m_impl->view.presenter = nullptr;
        [m_impl->view removeFromSuperview];
        m_impl->view = nil;
    }
    if (m_impl->statusItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:m_impl->statusItem];
        m_impl->statusItem = nil;
    }
}

qreal MacBarPresenter::heightPt() const
{
    if (m_impl->view)
        return m_impl->view.bounds.size.height;
    const CGFloat thickness = [NSStatusBar systemStatusBar].thickness;
    return thickness > 0 ? thickness : kFallbackThickness;
}

qreal MacBarPresenter::devicePixelRatio() const
{
    if (m_impl->view && m_impl->view.window)
        return m_impl->view.window.backingScaleFactor;
    return [NSScreen mainScreen].backingScaleFactor;
}

void MacBarPresenter::setWidthPt(qreal widthPt)
{
    m_impl->widthPt = widthPt;
    if (m_impl->statusItem)
        m_impl->statusItem.length = widthPt;
    if (m_impl->view) {
        NSRect frame = m_impl->view.frame;
        frame.size.width = widthPt;
        m_impl->view.frame = frame;
    }
}

bool MacBarPresenter::prefersCompact() const
{
    // safeAreaInsets.top is non-zero only where the camera housing intrudes,
    // which is exactly the case where menu bar real estate is scarce.
    NSScreen *screen = m_impl->view.window.screen ?: [NSScreen mainScreen];
    if (@available(macOS 12.0, *))
        return screen.safeAreaInsets.top > 0.0;
    return false;
}

void MacBarPresenter::present(const QImage &image)
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

    // QImage::Format_ARGB32_Premultiplied is BGRA in memory on a little-endian
    // machine, which is what these two flags together describe.
    CGImageRef cg = CGImageCreate(
        static_cast<std::size_t>(slot.width()), static_cast<std::size_t>(slot.height()), 8, 32,
        static_cast<std::size_t>(slot.bytesPerLine()), m_impl->colorSpace,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst)
            | static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little),
        provider, nullptr, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    if (!cg)
        return;

    [CATransaction begin];
    // Without this, Core Animation cross-fades every contents change over a
    // quarter second and a 60 fps visualiser turns into a permanent smear.
    [CATransaction setDisableActions:YES];
    m_impl->view.layer.contentsScale = devicePixelRatio();
    m_impl->view.layer.contents = (__bridge id)cg;
    [CATransaction commit];

    CGImageRelease(cg);
}

bool MacBarPresenter::isVisibleOnScreen() const
{
    if (!m_impl->statusItem || !m_impl->view)
        return false;

    // statusItem.visible and nothing else.
    //
    // The obvious addition here is NSWindowOcclusionStateVisible on the item's
    // window, to skip drawing under a fullscreen app with the menu bar hidden.
    // Do not: a status item's window does not reliably carry that bit at all.
    // Measured on macOS 26 it was clear on 240 of 241 consecutive frames with
    // the strip plainly on screen, so using it as a gate silently blanked the
    // whole visualiser -- the app ran, the tap worked, and nothing was ever
    // drawn. An optimisation that can turn the product off is not one.
    return m_impl->statusItem.visible;
}

void MacBarPresenter::startClock()
{
    if (m_impl->clockRunning || !m_impl->view || !m_impl->hook)
        return;

    if (@available(macOS 14.0, *)) {
        m_impl->link = [m_impl->view displayLinkWithTarget:m_impl->hook selector:@selector(step:)];

        // The analysis publishes at 60 Hz, so on a ProMotion panel every second
        // callback would find the same frame and turn straight back around.
        // Asking for 60 halves the callbacks rather than paying for them and
        // discarding the results.
        const auto fps = static_cast<float>(m_impl->preferredFps);
        m_impl->link.preferredFrameRateRange = CAFrameRateRangeMake(fps, fps, fps);

        // Common modes matters. In the default mode the link stops firing while
        // a menu is being tracked, so the strip would freeze at precisely the
        // moment someone clicked it to look at it.
        [m_impl->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        m_impl->clockRunning = m_impl->link != nil;
    }

    if (!m_impl->clockRunning)
        qWarning("hyperlight: no display link available; the strip will not animate");
}

void MacBarPresenter::setPreferredFps(int fps)
{
    fps = std::clamp(fps, 15, 120);
    if (fps == m_impl->preferredFps)
        return;
    m_impl->preferredFps = fps;
    // The range is set when the link is built, so a live link has to be
    // rebuilt rather than retuned.
    if (m_impl->clockRunning) {
        stopClock();
        startClock();
    }
}
void MacBarPresenter::stopClock()
{
    if (m_impl->link) {
        [m_impl->link invalidate];
        m_impl->link = nil;
    }
    m_impl->clockRunning = false;
}

bool MacBarPresenter::isClockRunning() const
{
    return m_impl->clockRunning;
}

void MacBarPresenter::setMenu(const MenuModel &model)
{
    if (!m_impl->statusItem)
        return;

    // Rebuilt wholesale every time. The app list changes as things start and
    // stop playing, and diffing an NSMenu against a model is considerably more
    // code than throwing the old one away.
    m_impl->menu = buildNSMenu(model, m_impl->hook, @selector(menuItemChosen:), m_impl->hook);

    // Deliberately NOT statusItem.menu.
    //
    // Assigning the menu makes AppKit own the click, which swallows the button
    // action and, more to the point, holds the item highlighted for as long as
    // the menu is tracking. Popping it up by hand on click keeps the strip
    // behaving like a strip.
}

void MacBarPresenter::showMenu()
{
    if (!m_impl->menu || !m_impl->statusItem)
        return;
    // popUpContextMenu, not popUpMenuPositioningItem:atLocation:inView:.
    //
    // The latter anchors the menu's corner at a point we choose, and choosing
    // one inside a status item put the menu's top edge above the top of the
    // screen: AppKit responded by making it scrollable, so the first item --
    // the source picker -- was hidden behind a scroll arrow. popUpContextMenu
    // lets AppKit place it the way it places every other status item menu.
    NSEvent *event = [NSApp currentEvent]
        ?: [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                              location:NSZeroPoint
                         modifierFlags:0
                             timestamp:0
                          windowNumber:m_impl->statusItem.button.window.windowNumber
                               context:nil
                           eventNumber:0
                            clickCount:1
                              pressure:1];
    [NSMenu popUpContextMenu:m_impl->menu withEvent:event forView:m_impl->statusItem.button];
}
void MacBarPresenter::emitMenuAction(const QString &actionId)
{
    Q_EMIT menuActionTriggered(actionId);
}

void MacBarPresenter::emitMenuAboutToShow()
{
    Q_EMIT menuAboutToShow();
}

void MacBarPresenter::emitFrameDue()
{
    Q_EMIT frameDue();
}

void MacBarPresenter::emitGeometryChanged()
{
    Q_EMIT geometryChanged();
}

void MacBarPresenter::emitClicked()
{
    // Order matters: the app rebuilds the menu in response to menuAboutToShow,
    // and only then is it popped up. Doing the rebuild from NSMenuDelegate's
    // menuWillOpen: instead would mean mutating a menu that is already tracking.
    Q_EMIT menuAboutToShow();
    showMenu();
    Q_EMIT clicked();
}

std::unique_ptr<BarPresenter> BarPresenter::create(QObject *parent)
{
    return std::make_unique<MacBarPresenter>(parent);
}

} // namespace hyperlight
