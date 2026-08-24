#include "platform/MacBarPresenter.h"

#include <algorithm>
#include <cmath>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <QDebug>

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
@interface HLPresenterHook : NSObject
@property (nonatomic, assign) hyperlight::MacBarPresenter *presenter;
- (void)step:(id)sender;
- (void)statusItemClicked:(id)sender;
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
    if (!m_impl->statusItem.visible)
        return false;
    NSWindow *window = m_impl->view.window;
    if (!window)
        return false;
    // Best effort rather than gospel: how faithfully occlusion is reported for
    // a status item window is not something to build correctness on, so this
    // only ever gates an optimisation.
    return (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
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

void MacBarPresenter::setMenu(QMenu *menu)
{
    (void)menu;
    // The status item's menu is built natively in App rather than handed over
    // from QMenu: Qt does its own menu merging and template-image handling on
    // macOS, and having already been bitten by setTemplate: once there is no
    // reason to give it a second opportunity.
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
    Q_EMIT clicked();
}

std::unique_ptr<BarPresenter> BarPresenter::create(QObject *parent)
{
    return std::make_unique<MacBarPresenter>(parent);
}

} // namespace hyperlight
