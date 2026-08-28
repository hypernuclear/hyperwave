#include "platform/PanelPresenter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include <QAbstractNativeEventFilter>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QWindow>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
#include <QSvgRenderer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidget>

// Windows display: a docked application desktop toolbar, plus a static tray
// icon as the anchor.
//
// The appbar reserves screen space the way the taskbar does, so maximised
// windows start below it. That is the real cost and it is also the whole point:
// it is the only surface that is guaranteed visible, where a floating strip
// loses to anything topmost and a desktop widget loses to the first maximised
// window. ABN_FULLSCREENAPP handles games for free. Deskbands -- the thing that
// genuinely lived inside the taskbar -- were removed in Windows 11, and Widgets
// are Adaptive Cards with a refresh budget in minutes that cannot animate.
//
// First run does NOT dock. An appbar is an unfamiliar convention and "this app
// permanently took 24 pixels of your screen" is a bad thing to discover rather
// than to choose, so the strip starts floating and the tray menu offers the
// edge. That is also why the tray icon exists at all: it is what offers
// docking, what undocks again, and what remains when ABM_NEW is refused because
// another appbar already owns that edge.
//
// The tray icon does not animate. At 16-24 px none of the six looks reads, and
// Shell_NotifyIcon is a cross-process call into explorer.exe. Drawn once.
//
// See docs/windows-backend.md.

namespace hyperwave {

namespace {

// Our appbar's callback message. Any WM_USER-relative value works; the shell
// only ever sends it back to the window that registered it.
constexpr UINT kAppBarCallback = WM_USER + 0x51;

constexpr int kDefaultHeightPt = 24;
constexpr int kDefaultWidthPt = 260;

// Where the placement choice is remembered. The same store Settings uses; the
// "win/" group keeps platform keys apart from the shared ones.
constexpr const char *kPositionKey = "win/position";
// The first cut of this file stored a docked flag and an edge before the
// taskbar mode existed. Read once, for migration; never written again.
constexpr const char *kDockedKey = "win/docked";
constexpr const char *kDockEdgeKey = "win/dockEdge";

// The strip's placement, as stored. Small ints rather than an enum class so
// QSettings round-trips them without ceremony.
enum PositionId : int {
    kPositionFloating = 0,
    kPositionTaskbar = 1,
    kPositionDockTop = 2,
    kPositionDockBottom = 3,
};

// Following the taskbar is a poll, and these are its two speeds: fast while
// its rectangle is in motion -- the auto-hide slide -- and slow once it has
// settled, so tracking an animation does not cost sixty wakeups a second
// forever after.
constexpr int kTaskbarPollSlowMs = 250;
constexpr int kTaskbarPollFastMs = 16;

// The floating panel: a notch for a machine that does not have one. The bars
// sit inside an outlined dark box that hangs flush from the screen edge --
// square shoulders against the edge, rounded corners away from it, the same
// shape the macOS notch panel draws. Draggable anywhere; within kSnapPx of the
// top or bottom edge it snaps flush and takes that edge's shape.
constexpr int kPanelPadH = 12;
constexpr int kPanelPadV = 4;
constexpr int kSnapPx = 40;
constexpr qreal kPanelRadius = 10.0;
// Where the panel was left, in logical coordinates. The snap state is not
// stored because the y coordinate already encodes it: a y at the screen edge
// re-snaps on restore, and a y beyond a smaller replacement screen clamps to
// its edge and snaps there instead of stranding the panel off-screen.
constexpr const char *kFloatXKey = "win/floatX";
constexpr const char *kFloatYKey = "win/floatY";

// Whether the taskbar -- and therefore the tray -- is currently light. This is
// SystemUsesLightTheme, not AppsUseLightTheme: Windows lets the two differ,
// and the tray icon sits on the taskbar, not in an app window. Absent means
// dark, which has been the shell default since the setting existed.
bool taskbarIsLight()
{
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion"
                       "\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("SystemUsesLightTheme"), 0).toInt() != 0;
}

// The glyph, tinted for the taskbar it will sit on: white on a dark taskbar,
// near-black on a light one. Rendered at every size the shell plausibly asks
// for, so a scaled display gets a crisp 24 px rather than an upscaled 16.
//
// Deliberately not the visualiser: at this size twenty-eight bands is eight
// bars of three pixels, and Shell_NotifyIcon is a cross-process call into
// explorer for every update. Drawn once per theme.
QIcon tintedTrayIcon(bool lightTaskbar)
{
    QSvgRenderer renderer(QStringLiteral(":/icons/hyperwave.svg"));
    const QColor tint = lightTaskbar ? QColor(0x1b, 0x1b, 0x1b) : QColor(Qt::white);
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(0, 0, size, size));
        // SourceIn keeps the glyph's alpha and replaces its colour, so the
        // SVG itself never needs to know about themes.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), tint);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

class WinBarPresenter;

// The strip itself.
//
// Frameless, topmost, and never focus-stealing. Qt::Tool keeps it off the
// taskbar; WA_TranslucentBackground lets the rendered image carry its own
// alpha, which is what makes the undocked strip read as a decoration rather
// than as a window someone forgot to close.
class StripWindow : public QWidget
{
public:
    explicit StripWindow(WinBarPresenter *owner)
        : QWidget(nullptr,
                  Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint
                      | Qt::WindowDoesNotAcceptFocus)
        , m_owner(owner)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_AlwaysStackOnTop);
    }

    void setImage(const QImage &image)
    {
        m_image = image;
        update();
    }

    void setNowPlayingText(const QString &text)
    {
        if (m_nowPlaying == text)
            return;
        m_nowPlaying = text;
        update();
    }

    // What to draw behind the bars. None in the docked and taskbar modes,
    // where the strip is a transparent overlay on someone else's bar; a box
    // in the floating mode, shaped by which edge it hangs from.
    enum class Panel { None, Free, TopNotch, BottomNotch };

    void setPanel(Panel panel)
    {
        if (m_panel == panel)
            return;
        m_panel = panel;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    WinBarPresenter *m_owner = nullptr;
    QImage m_image;
    QString m_nowPlaying;
    Panel m_panel = Panel::None;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_pressWindowPos;
};

// Routes the shell's appbar notifications back to the presenter.
//
// Qt gives us no hook for a window message it does not understand, so this sits
// in the application's native event filter and matches on our own hwnd.
class AppBarFilter : public QAbstractNativeEventFilter
{
public:
    explicit AppBarFilter(WinBarPresenter *owner) : m_owner(owner) {}
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

private:
    WinBarPresenter *m_owner = nullptr;
};

class WinBarPresenter : public PanelPresenter
{
public:
    explicit WinBarPresenter(QObject *parent) : PanelPresenter(parent) {}

    ~WinBarPresenter() override
    {
        // Not optional, and not merely tidy: an appbar that fails to
        // unregister leaves its reserved strip behind after the process is
        // gone, and the only way back is a reboot or another appbar claiming
        // the same edge.
        undock();
        if (m_filter) {
            if (QCoreApplication *app = QCoreApplication::instance())
                app->removeNativeEventFilter(m_filter.get());
            m_filter.reset();
        }
    }

    // --- BarPresenter -----------------------------------------------------

    bool install() override
    {
        if (m_window)
            return true;

        m_window = std::make_unique<StripWindow>(this);
        restoreFloating();
        m_window->show();

        m_filter = std::make_unique<AppBarFilter>(this);
        if (QCoreApplication *app = QCoreApplication::instance())
            app->installNativeEventFilter(m_filter.get());

        installTray();

        m_taskbarTimer.setTimerType(Qt::CoarseTimer);
        QObject::connect(&m_taskbarTimer, &QTimer::timeout, this, [this] { followTaskbar(); });

        // A placement chosen last session is restored. The never-dock-silently
        // rule is about the first launch, not about forgetting the user's
        // answer on every launch after it.
        const QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
        int position = store.value(QLatin1String(kPositionKey), -1).toInt();
        if (position < kPositionFloating || position > kPositionDockBottom) {
            const bool legacyDocked = store.value(QLatin1String(kDockedKey), false).toBool();
            const uint legacyEdge = store.value(QLatin1String(kDockEdgeKey), ABE_TOP).toUInt();
            position = !legacyDocked ? kPositionFloating
                : legacyEdge == ABE_BOTTOM ? kPositionDockBottom
                                           : kPositionDockTop;
        }
        applyPosition(position);

        m_clock.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_clock, &QTimer::timeout, this, [this] { Q_EMIT frameDue(); });
        return true;
    }

    void remove() override
    {
        stopClock();
        undock();
        m_tray.reset();
        m_window.reset();
    }

    qreal heightPt() const override { return kDefaultHeightPt; }

    qreal devicePixelRatio() const override
    {
        if (m_window && m_window->windowHandle())
            return m_window->windowHandle()->devicePixelRatio();
        if (auto *screen = QGuiApplication::primaryScreen())
            return screen->devicePixelRatio();
        return 1.0;
    }

    void setWidthPt(qreal widthPt) override
    {
        m_widthPt = std::max<int>(24, static_cast<int>(widthPt));
        if (!m_window)
            return;
        if (m_docked) {
            applyDockedGeometry();
        } else if (m_onTaskbar) {
            // Anchored by its right edge, so a width change moves the left
            // edge rather than growing rightward into the tray. Forgetting
            // the cached rect forces the follow to reposition.
            m_window->resize(m_widthPt, kDefaultHeightPt);
            m_lastTaskbarRect = {};
            followTaskbar();
        } else {
            // Keep the panel's centre where it was; a look with a different
            // width should breathe around its middle, not lurch sideways.
            const QPoint center = m_window->geometry().center();
            placeFloating(center.x() - floatingSize().width() / 2, m_window->y(), false);
        }
    }

    void present(const QImage &image) override
    {
        if (m_window)
            m_window->setImage(image);
    }

    bool isVisibleOnScreen() const override
    {
        // Riding an auto-hidden taskbar, the strip is parked off the edge of
        // the screen along with it; rendering frames into a window the screen
        // cannot show is pure heat.
        return m_window && m_window->isVisible() && !m_fullscreenAppActive
               && (!m_onTaskbar || m_taskbarStripVisible);
    }

    void setPreferredFps(int fps) override
    {
        m_fps = std::clamp(fps, 1, 240);
        if (m_clock.isActive())
            m_clock.start(1000 / m_fps);
    }

    void startClock() override
    {
        // No display link on Windows. DwmGetCompositionTimingInfo would let us
        // phase-lock to the compositor; a plain timer costs the occasional
        // dropped frame and nothing else, so that is a later refinement rather
        // than a prerequisite.
        if (!m_clock.isActive())
            m_clock.start(1000 / m_fps);
    }

    void stopClock() override { m_clock.stop(); }
    bool isClockRunning() const override { return m_clock.isActive(); }

    void setMenu(const MenuModel &model) override
    {
        // Rebuilt rather than mutated, and safe to delete here because the menu
        // is never open at this point: the popup paths below emit
        // menuAboutToShow first and pop up afterwards, so any rebuild the app
        // does in response has already finished before anything is shown.
        auto menu = std::make_unique<QMenu>();
        populate(menu.get(), model);
        appendWindowsItems(menu.get());
        m_menu = std::move(menu);
    }

    // --- PanelPresenter ---------------------------------------------------

    void setTabVisible(bool visible) override
    {
        m_tabVisible = visible;
        if (!m_window)
            return;
        // Docked, the strip stays put: retracting it would hand its reserved
        // space back and shove every maximised window up by 24 px every time
        // the music paused.
        if (m_docked)
            m_window->show();
        else
            m_window->setVisible(visible);
    }

    void setNowPlaying(const NowPlayingInfo &info, const QString &fallbackAppName) override
    {
        const QString title = info.title.isEmpty() ? fallbackAppName : info.title;
        const QString text = info.artist.isEmpty() || info.title.isEmpty()
            ? title
            : QStringLiteral("%1 - %2").arg(title, info.artist);

        if (m_window)
            m_window->setNowPlayingText(text);
        if (m_tray) {
            m_tray->setToolTip(text.isEmpty() ? QStringLiteral("hyperwave")
                                              : QStringLiteral("hyperwave - %1").arg(text));
        }
    }

    void setDevices(const AudioDeviceList &devices, const QString &current) override
    {
        // The strip has no picker of its own yet; the tray menu's Source
        // submenu is built by the app from the same list. Kept so the appbar
        // can grow one without another round through the interface.
        m_devices = devices;
        m_currentDevice = current;
    }

    // --- appbar -----------------------------------------------------------

    // Called from the native event filter.
    void onAppBarNotification(WPARAM notification)
    {
        switch (notification) {
        case ABN_POSCHANGED:
        case ABN_WINDOWARRANGE:
            // The taskbar moved, another appbar appeared, or the resolution
            // changed. Re-ask for our space rather than assuming we still have
            // the rectangle we were given.
            if (m_docked)
                applyDockedGeometry();
            break;
        case ABN_FULLSCREENAPP:
            // A game or a fullscreen video went up or came down. This arriving
            // for free is one of the reasons the appbar won: every other
            // surface means hand-rolling this detection.
            m_fullscreenAppActive = !m_fullscreenAppActive;
            if (m_window)
                m_window->setVisible(!m_fullscreenAppActive && (m_docked || m_tabVisible));
            break;
        default:
            break;
        }
    }

    HWND hwnd() const
    {
        return m_window ? reinterpret_cast<HWND>(m_window->winId()) : nullptr;
    }

    void showMenuAtCursor()
    {
        if (!m_menu)
            return;
        // Ask for a fresh menu, then show what came back -- the same ordering
        // the macOS surfaces use. Without it the menu is a snapshot of whenever
        // it was last built: stale check marks and devices that are gone.
        Q_EMIT menuAboutToShow();
        if (m_menu)
            m_menu->popup(QCursor::pos());
    }

    void emitClicked() { Q_EMIT clicked(); }

    // Called from the native event filter on WM_SETTINGCHANGE. That message
    // arrives in bursts, so re-read the registry and only touch the icon on an
    // actual flip -- every setIcon is an IPC round trip into explorer.
    void onThemeMaybeChanged()
    {
        if (!m_tray)
            return;
        const bool light = taskbarIsLight();
        if (light == m_trayIsLight)
            return;
        m_trayIsLight = light;
        m_tray->setIcon(tintedTrayIcon(light));
    }

private:
    // --- menu -------------------------------------------------------------

    void populate(QMenu *menu, const MenuModel &model)
    {
        for (const MenuItem &item : model) {
            switch (item.kind) {
            case MenuItem::Kind::Separator:
                menu->addSeparator();
                break;
            case MenuItem::Kind::Submenu: {
                QMenu *sub = menu->addMenu(item.title);
                populate(sub, item.children);
                break;
            }
            case MenuItem::Kind::Action: {
                const QString label = item.detail.isEmpty()
                    ? item.title
                    : QStringLiteral("%1\t%2").arg(item.title, item.detail);
                QAction *action = menu->addAction(label);
                action->setEnabled(item.enabled);
                if (item.checked) {
                    action->setCheckable(true);
                    action->setChecked(true);
                }
                const QString id = item.actionId;
                QObject::connect(action, &QAction::triggered, this,
                                 [this, id] { Q_EMIT menuActionTriggered(id); });
                break;
            }
            }
        }
    }

    // Placement is this platform's business, not the app's, so these items are
    // added here rather than in the shared MenuModel.
    void appendWindowsItems(QMenu *menu)
    {
        menu->addSeparator();

        QMenu *position = menu->addMenu(tr("Position"));
        struct Choice
        {
            int id;
            const char *label;
        };
        static const Choice kChoices[] = {
            {kPositionFloating, "Floating panel"},
            {kPositionTaskbar, "On the taskbar"},
            {kPositionDockTop, "Own bar, top of screen"},
            {kPositionDockBottom, "Own bar, bottom of screen"},
        };
        const int current = currentPositionId();
        for (const Choice &choice : kChoices) {
            QAction *action = position->addAction(tr(choice.label));
            action->setCheckable(true);
            action->setChecked(current == choice.id);
            const int id = choice.id;
            QObject::connect(action, &QAction::triggered, this, [this, id] {
                applyPosition(id);
                rememberPosition();
            });
        }

        // Dragging is the primary way to move the panel; these exist for
        // anyone who wants the canonical spots without steadying a hand.
        if (current == kPositionFloating && m_window) {
            position->addSeparator();
            struct Align
            {
                const char *label;
                qreal fraction;
            };
            static const Align kAligns[] = {
                {"Align left", 0.0},
                {"Align center", 0.5},
                {"Align right", 1.0},
            };
            for (const Align &align : kAligns) {
                QAction *action = position->addAction(tr(align.label));
                const qreal fraction = align.fraction;
                QObject::connect(action, &QAction::triggered, this, [this, fraction] {
                    QScreen *screen = m_window->screen();
                    if (!screen)
                        return;
                    const QRect geo = screen->geometry();
                    const int span = geo.width() - floatingSize().width() - 2 * kPanelPadH;
                    placeFloating(geo.left() + kPanelPadH + qRound(span * fraction),
                                  m_window->y(), true);
                });
            }
        }
    }

    int currentPositionId() const
    {
        if (m_docked)
            return m_edge == ABE_BOTTOM ? kPositionDockBottom : kPositionDockTop;
        return m_onTaskbar ? kPositionTaskbar : kPositionFloating;
    }

    void applyPosition(int position)
    {
        // Leave whatever mode is active first, so the modes never overlap.
        if (m_onTaskbar) {
            m_onTaskbar = false;
            m_taskbarTimer.stop();
        }
        if (m_docked && position != kPositionDockTop && position != kPositionDockBottom)
            undock();

        switch (position) {
        case kPositionTaskbar:
            // No box on someone else's bar: there the strip is a transparent
            // overlay, not a panel of its own.
            if (m_window)
                m_window->setPanel(StripWindow::Panel::None);
            m_onTaskbar = true;
            m_taskbarHwnd = nullptr;
            m_lastTaskbarRect = {};
            followTaskbar();
            m_taskbarTimer.start(kTaskbarPollSlowMs);
            break;
        case kPositionDockTop:
            if (m_window)
                m_window->setPanel(StripWindow::Panel::None);
            dockTo(ABE_TOP);
            break;
        case kPositionDockBottom:
            if (m_window)
                m_window->setPanel(StripWindow::Panel::None);
            dockTo(ABE_BOTTOM);
            break;
        default:
            restoreFloating();
            break;
        }
    }

    // The end of a drag: snap to whatever edge the panel landed near, and
    // remember where it was left.
    void onStripDragged()
    {
        if (m_docked || m_onTaskbar || !m_window)
            return;
        placeFloating(m_window->x(), m_window->y(), true);
    }

    // Written only from the menu handler, never from teardown, and it records
    // what actually happened rather than what was asked for: a refused
    // ABM_NEW leaves the strip floating, and remembering the refusal as a
    // choice to dock would re-fight that fight on every launch.
    void rememberPosition()
    {
        QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
        store.setValue(QLatin1String(kPositionKey), currentPositionId());
    }

    // Sit on the taskbar itself. There is no supported way to live INSIDE the
    // Windows 11 taskbar -- deskbands, the thing that genuinely did, were
    // removed -- so the strip overlays the taskbar's own window and follows
    // it: re-anchored whenever the bar moves or resizes, sliding off the
    // screen edge with it when auto-hide slides it away. Polling rather than
    // hooks for the same reason device changes are polled: the shell repaints
    // and rearranges on its own schedule, and a quarter second of lag on "the
    // taskbar moved" costs nothing next to what a hook into explorer costs.
    void followTaskbar()
    {
        if (!m_onTaskbar || !m_window)
            return;
        if (!m_taskbarHwnd || !IsWindow(m_taskbarHwnd))
            m_taskbarHwnd = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!m_taskbarHwnd)
            return; // explorer is restarting; the next poll finds the new one

        RECT taskbar = {};
        if (!GetWindowRect(m_taskbarHwnd, &taskbar))
            return;

        const bool moved = std::memcmp(&taskbar, &m_lastTaskbarRect, sizeof(RECT)) != 0;
        m_lastTaskbarRect = taskbar;
        const int wantedInterval = moved ? kTaskbarPollFastMs : kTaskbarPollSlowMs;
        if (m_taskbarTimer.interval() != wantedInterval)
            m_taskbarTimer.setInterval(wantedInterval);
        if (!moved)
            return;

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatio());

        // Right-aligned, just inboard of the notification area -- where a
        // deskband would have sat, and the mirror of where the macOS status
        // item lives. The tray is a child window that can be measured; if the
        // shell ever renames it, fall back to a guess wide enough for the
        // clock and a handful of icons.
        LONG trayLeft = taskbar.right - static_cast<LONG>(qRound(220 * dpr));
        if (HWND notify = FindWindowExW(m_taskbarHwnd, nullptr, L"TrayNotifyWnd", nullptr)) {
            RECT notifyRect = {};
            if (GetWindowRect(notify, &notifyRect) && notifyRect.left > taskbar.left)
                trayLeft = notifyRect.left;
        }

        const LONG stripWidth = static_cast<LONG>(qRound(m_widthPt * dpr));
        const LONG stripHeight = static_cast<LONG>(qRound(kDefaultHeightPt * dpr));
        const LONG x = trayLeft - stripWidth - static_cast<LONG>(qRound(8 * dpr));
        const LONG y = taskbar.top + ((taskbar.bottom - taskbar.top) - stripHeight) / 2;

        // The shell speaks physical pixels and Qt speaks logical ones, same
        // as the appbar path.
        m_window->setGeometry(qRound(x / dpr), qRound(y / dpr), m_widthPt, kDefaultHeightPt);

        QScreen *screen = QGuiApplication::primaryScreen();
        m_taskbarStripVisible = screen && m_window->geometry().intersects(screen->geometry());

        // The taskbar is topmost too, and whenever it reappears it lands on
        // top of the topmost pile. Re-assert, or the strip rides BEHIND the
        // bar it is sitting on.
        SetWindowPos(hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void installTray()
    {
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            return;

        m_trayIsLight = taskbarIsLight();
        m_tray = std::make_unique<QSystemTrayIcon>();
        m_tray->setIcon(tintedTrayIcon(m_trayIsLight));
        m_tray->setToolTip(QStringLiteral("hyperwave"));

        // No setContextMenu: Qt would pop the menu itself, before the app has
        // had a chance to rebuild it. Handling activation ourselves keeps the
        // refresh-then-show ordering.
        //
        // Left-click opens the same menu as right-click. This app has no main
        // window for a left-click to summon, and a tray icon that does nothing
        // when clicked reads as broken -- it is also how someone finds the
        // dock option in the first place.
        QObject::connect(m_tray.get(), &QSystemTrayIcon::activated, this,
                         [this](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Context
                                 || reason == QSystemTrayIcon::Trigger)
                                 showMenuAtCursor();
                         });
        m_tray->show();
    }

    // --- geometry ---------------------------------------------------------

    // The floating panel's outer size: the bars plus the box around them.
    // heightPt() still reports the bars' own height -- the padding belongs to
    // the box, not to the image the renderer is asked for.
    QSize floatingSize() const
    {
        return QSize(m_widthPt + 2 * kPanelPadH, kDefaultHeightPt + 2 * kPanelPadV);
    }

    // Put the floating panel at (x, y): clamp into the screen, snap flush to
    // the top or bottom edge when within kSnapPx of it, and take the shape of
    // wherever it ended up -- square shoulders against the edge it hangs from,
    // rounded corners everywhere else. The screen is the one under the panel,
    // so a drag to another monitor sticks rather than being yanked home.
    void placeFloating(int x, int y, bool remember)
    {
        if (!m_window)
            return;
        const QSize size = floatingSize();
        QScreen *screen = QGuiApplication::screenAt(
            QPoint(x + size.width() / 2, y + size.height() / 2));
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;

        // geometry(), not availableGeometry(): the whole point of this shape
        // is hanging flush from the physical screen edge, the way the macOS
        // notch panel hangs from the bezel, on top of any maximised title bar.
        const QRect geo = screen->geometry();
        x = std::clamp(x, geo.left(), geo.left() + geo.width() - size.width());
        y = std::clamp(y, geo.top(), geo.top() + geo.height() - size.height());

        StripWindow::Panel shape = StripWindow::Panel::Free;
        if (y - geo.top() <= kSnapPx) {
            y = geo.top();
            shape = StripWindow::Panel::TopNotch;
        } else if ((geo.top() + geo.height()) - (y + size.height()) <= kSnapPx) {
            y = geo.top() + geo.height() - size.height();
            shape = StripWindow::Panel::BottomNotch;
        }

        m_window->setPanel(shape);
        m_window->setGeometry(x, y, size.width(), size.height());

        if (remember) {
            QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
            store.setValue(QLatin1String(kFloatXKey), x);
            store.setValue(QLatin1String(kFloatYKey), y);
        }
    }

    void restoreFloating()
    {
        if (!m_window)
            return;
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QRect geo = screen->geometry();
        const QSize size = floatingSize();
        // Default: hanging from the top edge, centred. A notch, for a machine
        // that does not have one.
        const QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
        const int x = store.value(QLatin1String(kFloatXKey),
                                  geo.center().x() - size.width() / 2).toInt();
        const int y = store.value(QLatin1String(kFloatYKey), geo.top()).toInt();
        placeFloating(x, y, false);
    }

    void dockTo(UINT edge)
    {
        m_edge = edge;
        if (!m_window)
            return;

        if (!m_docked) {
            APPBARDATA abd = {};
            abd.cbSize = sizeof(abd);
            abd.hWnd = hwnd();
            abd.uCallbackMessage = kAppBarCallback;
            if (!SHAppBarMessage(ABM_NEW, &abd)) {
                // Refused, most likely because another appbar already owns the
                // edge. The tray icon is why this is recoverable rather than
                // fatal: the app is still reachable, still running, and still
                // has a floating strip.
                return;
            }
            m_docked = true;
        }
        applyDockedGeometry();
        m_window->show();
    }

    void undock()
    {
        if (!m_docked)
            return;
        APPBARDATA abd = {};
        abd.cbSize = sizeof(abd);
        abd.hWnd = hwnd();
        SHAppBarMessage(ABM_REMOVE, &abd);
        m_docked = false;
        restoreFloating();
    }

    // Ask the shell where we may sit, accept its answer, then move there.
    //
    // The QUERYPOS/SETPOS pair is not ceremony: the shell adjusts the proposed
    // rectangle around the taskbar and any other appbar, and a window placed at
    // the un-adjusted rectangle overlaps whatever it did not know about.
    void applyDockedGeometry()
    {
        if (!m_window || !m_docked)
            return;
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;

        // The shell speaks physical pixels and Qt speaks logical ones. At 100%
        // they are the same number, which is exactly why getting this wrong is
        // invisible until it reaches a scaled display -- and most Windows
        // laptops ship scaled. Convert going in, and convert coming back.
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatio());
        const QRect logical = screen->geometry();
        const auto toPhysical = [dpr](int v) { return static_cast<LONG>(qRound(v * dpr)); };
        const auto thickness = static_cast<LONG>(qRound(kDefaultHeightPt * dpr));

        APPBARDATA abd = {};
        abd.cbSize = sizeof(abd);
        abd.hWnd = hwnd();
        abd.uEdge = m_edge;
        abd.rc.left = toPhysical(logical.left());
        abd.rc.right = toPhysical(logical.left() + logical.width());
        if (m_edge == ABE_TOP) {
            abd.rc.top = toPhysical(logical.top());
            abd.rc.bottom = abd.rc.top + thickness;
        } else {
            abd.rc.bottom = toPhysical(logical.top() + logical.height());
            abd.rc.top = abd.rc.bottom - thickness;
        }

        SHAppBarMessage(ABM_QUERYPOS, &abd);

        // QUERYPOS moves the edge we asked about to dodge the taskbar and any
        // other appbar. Restore our thickness against what it gave back, or the
        // bar loses a few pixels every time a notification arrives.
        if (m_edge == ABE_TOP)
            abd.rc.bottom = abd.rc.top + thickness;
        else
            abd.rc.top = abd.rc.bottom - thickness;

        SHAppBarMessage(ABM_SETPOS, &abd);

        m_window->setGeometry(qRound(abd.rc.left / dpr), qRound(abd.rc.top / dpr),
                              qRound((abd.rc.right - abd.rc.left) / dpr),
                              qRound((abd.rc.bottom - abd.rc.top) / dpr));
        m_widthPt = m_window->width();
        Q_EMIT geometryChanged();
    }

    std::unique_ptr<StripWindow> m_window;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu> m_menu;
    std::unique_ptr<AppBarFilter> m_filter;
    QTimer m_clock;
    QTimer m_taskbarTimer;

    int m_widthPt = kDefaultWidthPt;
    int m_fps = 60;
    bool m_docked = false;
    bool m_onTaskbar = false;
    bool m_taskbarStripVisible = true;
    bool m_tabVisible = true;
    bool m_fullscreenAppActive = false;
    bool m_trayIsLight = false;
    UINT m_edge = ABE_TOP;
    HWND m_taskbarHwnd = nullptr;
    RECT m_lastTaskbarRect = {};

    AudioDeviceList m_devices;
    QString m_currentDevice;

    friend class StripWindow;
};

// --- StripWindow members that need the presenter's definition --------------

void StripWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_panel != Panel::None) {
        // The box: our own black behind the bars, a hairline of light around
        // it, and rounded corners only on the sides facing away from the
        // screen edge -- the shape the macOS notch panel draws. The trick for
        // the square shoulders is to push the edge-facing side of the
        // rounded rect out of the window, where its rounding and its stroke
        // are clipped away, leaving flat sides running into the edge.
        painter.setRenderHint(QPainter::Antialiasing, true);
        QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (m_panel == Panel::TopNotch)
            box.adjust(0, -(kPanelRadius + 1.0), 0, 0);
        else if (m_panel == Panel::BottomNotch)
            box.adjust(0, 0, 0, kPanelRadius + 1.0);
        QPainterPath path;
        path.addRoundedRect(box, kPanelRadius, kPanelRadius);
        painter.fillPath(path, QColor(10, 10, 12, 216));
        painter.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
        painter.drawPath(path);
    }

    if (!m_image.isNull()) {
        // At the image's own logical size, centred -- never stretched to the
        // window. Floating, the window is sized to the image, so this is the
        // same blit it always was. Docked, the window spans the whole screen
        // edge, because that is what a reserved band is; stretching 260
        // points of bars across a 2560-pixel monitor was the first thing
        // docking got wrong. Centred in a transparent band is also the shape
        // of the thing being emulated: a menu bar the strip sits inside.
        const qreal dpr = std::max<qreal>(1.0, m_owner ? m_owner->devicePixelRatio() : 1.0);
        const QSizeF logical = QSizeF(m_image.size()) / dpr;
        const QRectF target(QPointF((width() - logical.width()) / 2.0,
                                    (height() - logical.height()) / 2.0),
                            logical);
        painter.drawImage(target, m_image);
    }

    if (!m_nowPlaying.isEmpty() && width() > 420) {
        // Only when there is genuinely room. A truncated track title next to
        // the bars is worse than no title.
        painter.setPen(QColor(255, 255, 255, 170));
        QFont font = painter.font();
        font.setPointSizeF(9.0);
        painter.setFont(font);
        const QRect textBox(width() / 2 + 8, 0, width() / 2 - 16, height());
        painter.drawText(textBox, Qt::AlignVCenter | Qt::AlignRight,
                         painter.fontMetrics().elidedText(m_nowPlaying, Qt::ElideRight,
                                                          textBox.width()));
    }
}

void StripWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressWindowPos = pos();
        m_dragging = false;
    }
    QWidget::mousePressEvent(event);
}

void StripWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Only the floating panel is draggable; docked and taskbar-riding strips
    // are anchored to something and a drag would fight it.
    if (!(event->buttons() & Qt::LeftButton) || m_panel == Panel::None)
        return;
    const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
    if (!m_dragging && delta.manhattanLength() < QApplication::startDragDistance())
        return;
    m_dragging = true;
    // Free movement while the button is down; the snap happens on release.
    // Snapping mid-drag reads as the window wrestling the cursor.
    move(m_pressWindowPos + delta);
}

void StripWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_owner) {
        // A click is a press that never became a drag. Deciding at release
        // rather than at press is what keeps the two gestures apart.
        if (m_dragging)
            m_owner->onStripDragged();
        else
            m_owner->emitClicked();
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void StripWindow::contextMenuEvent(QContextMenuEvent *event)
{
    // Right-click on a docked strip is what the taskbar itself does, so it is
    // the idiomatic gesture here -- more so than on macOS.
    if (m_owner)
        m_owner->showMenuAtCursor();
    event->accept();
}

bool AppBarFilter::nativeEventFilter(const QByteArray &type, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (type != QByteArrayLiteral("windows_generic_MSG") || !m_owner)
        return false;

    auto *msg = static_cast<MSG *>(message);
    if (msg->message == kAppBarCallback && msg->hwnd == m_owner->hwnd()) {
        m_owner->onAppBarNotification(msg->wParam);
        return false; // observed, not consumed
    }
    if (msg->message == WM_SETTINGCHANGE) {
        // lParam names what changed; a light/dark flip broadcasts
        // "ImmersiveColorSet" to every top-level window, ours included. The
        // presenter re-reads the actual state, so over-firing here is cheap.
        const auto *area = reinterpret_cast<const wchar_t *>(msg->lParam);
        if (area && lstrcmpiW(area, L"ImmersiveColorSet") == 0)
            m_owner->onThemeMaybeChanged();
        return false;
    }
    return false;
}

} // namespace

std::unique_ptr<BarPresenter> BarPresenter::create(QObject *parent)
{
    return std::make_unique<WinBarPresenter>(parent);
}

} // namespace hyperwave
