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
#include <QWheelEvent>
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

// Windows display: a floating notch panel, plus a static tray icon as the
// anchor.
//
// The panel is the macOS notch panel's shape as a topmost window: a dark
// outlined box hanging flush from the screen edge, square shoulders against
// the edge, rounded corners away from it, sitting over any maximised title
// bar. Draggable anywhere; near the top or bottom edge it snaps flush and
// takes that edge's shape. The mouse wheel over it resizes it.
//
// An appbar (SHAppBarMessage) and a strip riding the taskbar were both built
// first and both removed after use. The appbar reserved space the way the
// taskbar does -- guaranteed visibility, ABN_FULLSCREENAPP for free -- but it
// crashed in ordinary use, and an appbar's failure mode is uniquely bad: a
// process that dies without ABM_REMOVE leaves its reserved strip behind until
// a reboot. The taskbar rider was a z-order fight with a shell that repaints
// on its own schedule. The panel has neither failure mode, and what the
// appbar provided for free -- hiding over fullscreen games -- is a cheap
// SHQueryUserNotificationState poll. docs/windows-backend.md records the
// full reversal.
//
// The tray icon does not animate. At 16-24 px none of the six looks reads,
// and Shell_NotifyIcon is a cross-process call into explorer.exe. Drawn once
// per theme.

namespace hyperwave {

namespace {

constexpr int kDefaultHeightPt = 24;
constexpr int kDefaultWidthPt = 260;

// The floating panel: a notch for a machine that does not have one. The bars
// sit inside an outlined dark box; kPanelPad* is the box around them.
// Within kSnapPx of the top or bottom edge the panel snaps flush and takes
// that edge's shape.
constexpr int kPanelPadH = 12;
constexpr int kPanelPadV = 4;
constexpr int kSnapPx = 40;
constexpr qreal kPanelRadius = 10.0;

// The wheel resizes the panel between these bounds, an eighth per notch. The
// scale rides in devicePixelRatio(), so the renderer keeps its 24 pt world
// and simply produces more pixels -- crisp at any size, never upscaled.
constexpr qreal kMinScale = 0.75;
constexpr qreal kMaxScale = 2.5;
constexpr qreal kScaleStep = 0.125;

// How often to ask the shell whether a fullscreen app owns the display. The
// appbar used to be told (ABN_FULLSCREENAPP); a poll is the price of not
// being an appbar, and two seconds of latency on "a game started" is fine
// for a decoration.
constexpr int kFullscreenPollMs = 2000;

// Where the panel was left, in logical coordinates, plus its scale. The
// snap state is not stored because the y coordinate already encodes it: a y
// at the screen edge re-snaps on restore, and a y beyond a smaller
// replacement screen clamps to its edge and snaps there instead of
// stranding the panel off-screen.
constexpr const char *kFloatXKey = "win/floatX";
constexpr const char *kFloatYKey = "win/floatY";
constexpr const char *kFloatScaleKey = "win/floatScale";

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

// The panel itself.
//
// Frameless, topmost, and never focus-stealing. Qt::Tool keeps it off the
// taskbar; WA_TranslucentBackground lets the box carry its own alpha and
// rounded corners.
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

    // The box behind the bars, shaped by which edge the panel hangs from.
    enum class Panel { Free, TopNotch, BottomNotch };

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
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    WinBarPresenter *m_owner = nullptr;
    QImage m_image;
    QString m_nowPlaying;
    Panel m_panel = Panel::TopNotch;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_pressWindowPos;
};

// Watches WM_SETTINGCHANGE so the tray glyph can follow a light/dark theme
// flip without a restart. Qt gives us no hook for a window message it does
// not understand, so this sits in the application's native event filter.
class ThemeWatchFilter : public QAbstractNativeEventFilter
{
public:
    explicit ThemeWatchFilter(WinBarPresenter *owner) : m_owner(owner) {}
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

        QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
        m_scale = std::clamp(store.value(QLatin1String(kFloatScaleKey), 1.0).toDouble(),
                             kMinScale, kMaxScale);

        m_window = std::make_unique<StripWindow>(this);
        restoreFloating();
        m_window->show();

        m_filter = std::make_unique<ThemeWatchFilter>(this);
        if (QCoreApplication *app = QCoreApplication::instance())
            app->installNativeEventFilter(m_filter.get());

        installTray();

        m_fullscreenTimer.setTimerType(Qt::CoarseTimer);
        QObject::connect(&m_fullscreenTimer, &QTimer::timeout, this,
                         [this] { pollFullscreen(); });
        m_fullscreenTimer.start(kFullscreenPollMs);

        m_clock.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_clock, &QTimer::timeout, this, [this] { Q_EMIT frameDue(); });
        return true;
    }

    void remove() override
    {
        stopClock();
        m_fullscreenTimer.stop();
        m_tray.reset();
        m_window.reset();
    }

    qreal heightPt() const override { return kDefaultHeightPt; }

    qreal devicePixelRatio() const override
    {
        // The user's chosen panel scale rides in here. The renderer keeps its
        // 24 pt world and produces proportionally more pixels; the paint lays
        // them out at scale, so a doubled panel is drawn at double
        // resolution rather than upscaled.
        return windowDpr() * m_scale;
    }

    void setWidthPt(qreal widthPt) override
    {
        m_widthPt = std::max<int>(24, static_cast<int>(widthPt));
        if (!m_window)
            return;
        // Keep the panel's centre where it was; a look with a different
        // width should breathe around its middle, not lurch sideways.
        const QPoint center = m_window->geometry().center();
        placeFloating(center.x() - floatingSize().width() / 2, m_window->y(), false);
    }

    void present(const QImage &image) override
    {
        if (m_window)
            m_window->setImage(image);
    }

    bool isVisibleOnScreen() const override
    {
        return m_window && m_window->isVisible() && !m_fullscreenAppActive;
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
        m_quitAction = nullptr;
        populate(menu.get(), model);
        insertWindowsItems(menu.get());
        m_menu = std::move(menu);
    }

    // --- PanelPresenter ---------------------------------------------------

    void setTabVisible(bool visible) override
    {
        m_tabVisible = visible;
        if (m_window)
            m_window->setVisible(visible && !m_fullscreenAppActive);
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
        // The panel has no picker of its own yet; the menu's Source submenu is
        // built by the app from the same list. Kept so the panel can grow one
        // without another round through the interface.
        m_devices = devices;
        m_currentDevice = current;
    }

    // --- called back from the window and the filter ------------------------

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

    // The end of a drag: snap to whatever edge the panel landed near, and
    // remember where it was left.
    void onStripDragged()
    {
        if (m_window)
            placeFloating(m_window->x(), m_window->y(), true);
    }

    // The wheel over the panel. Deltas accumulate so a free-spinning wheel
    // and a trackpad both land on the same eighth-steps.
    void onStripWheel(int angleDelta)
    {
        m_wheelAccum += angleDelta;
        const int steps = m_wheelAccum / 120;
        if (steps == 0)
            return;
        m_wheelAccum -= steps * 120;

        const qreal next = std::clamp(m_scale + steps * kScaleStep, kMinScale, kMaxScale);
        if (next == m_scale)
            return;
        m_scale = next;

        QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
        store.setValue(QLatin1String(kFloatScaleKey), m_scale);

        rescale();
    }

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

    qreal windowDpr() const
    {
        if (m_window && m_window->windowHandle())
            return m_window->windowHandle()->devicePixelRatio();
        if (auto *screen = QGuiApplication::primaryScreen())
            return screen->devicePixelRatio();
        return 1.0;
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
                // Remembered so the platform items below can be inserted
                // above it rather than dangling after it.
                if (id == QLatin1String(MenuAction::kQuit))
                    m_quitAction = action;
                break;
            }
            }
        }
    }

    // Placement is this platform's business, not the app's, so these items
    // are added here rather than in the shared MenuModel -- and above Quit,
    // because nothing belongs below Quit.
    void insertWindowsItems(QMenu *menu)
    {
        auto *position = new QMenu(tr("Position"), menu);

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
        // Dragging is the primary way to move the panel; these exist for
        // anyone who wants the canonical spots without steadying a hand.
        for (const Align &align : kAligns) {
            QAction *action = position->addAction(tr(align.label));
            const qreal fraction = align.fraction;
            QObject::connect(action, &QAction::triggered, this, [this, fraction] {
                if (!m_window)
                    return;
                QScreen *screen = m_window->screen();
                if (!screen)
                    return;
                const QRect geo = screen->geometry();
                const int span = geo.width() - floatingSize().width() - 2 * kPanelPadH;
                placeFloating(geo.left() + kPanelPadH + qRound(span * fraction),
                              m_window->y(), true);
            });
        }

        position->addSeparator();
        QAction *reset = position->addAction(tr("Reset size"));
        QObject::connect(reset, &QAction::triggered, this, [this] {
            if (m_scale == 1.0)
                return;
            m_scale = 1.0;
            QSettings store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"));
            store.setValue(QLatin1String(kFloatScaleKey), m_scale);
            rescale();
        });

        if (m_quitAction) {
            menu->insertSeparator(m_quitAction);
            menu->insertMenu(m_quitAction, position);
        } else {
            menu->addSeparator();
            menu->addMenu(position);
        }
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
        // when clicked reads as broken.
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
    // the box, not to the image the renderer is asked for, and the scale
    // multiplies the content but not the box's breathing room.
    QSize floatingSize() const
    {
        return QSize(qRound(m_widthPt * m_scale) + 2 * kPanelPadH,
                     qRound(kDefaultHeightPt * m_scale) + 2 * kPanelPadV);
    }

    // Put the panel at (x, y): clamp into the screen, snap flush to the top
    // or bottom edge when within kSnapPx of it, and take the shape of
    // wherever it ended up -- square shoulders against the edge it hangs
    // from, rounded corners everywhere else. The screen is the one under the
    // panel, so a drag to another monitor sticks rather than being yanked
    // home.
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

    // A scale change moves every measurement at once: the window grows around
    // its centre, and the app is told the geometry changed so it re-renders
    // at the new pixel density rather than letting the paint stretch stale
    // pixels.
    void rescale()
    {
        if (!m_window)
            return;
        const QPoint center = m_window->geometry().center();
        placeFloating(center.x() - floatingSize().width() / 2, m_window->y(), true);
        Q_EMIT geometryChanged();
    }

    // The job ABN_FULLSCREENAPP used to do, now that there is no appbar to be
    // told: hide while a game or a presentation owns the screen, come back
    // afterwards. QUNS_BUSY is F11-style fullscreen; the other two are
    // exclusive D3D and presentation mode.
    void pollFullscreen()
    {
        QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
        if (FAILED(SHQueryUserNotificationState(&state)))
            return;
        const bool fullscreen = state == QUNS_RUNNING_D3D_FULL_SCREEN
            || state == QUNS_PRESENTATION_MODE || state == QUNS_BUSY;
        if (fullscreen == m_fullscreenAppActive)
            return;
        m_fullscreenAppActive = fullscreen;
        if (m_window)
            m_window->setVisible(!fullscreen && m_tabVisible);
    }

    std::unique_ptr<StripWindow> m_window;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu> m_menu;
    std::unique_ptr<ThemeWatchFilter> m_filter;
    QTimer m_clock;
    QTimer m_fullscreenTimer;
    QAction *m_quitAction = nullptr;

    int m_widthPt = kDefaultWidthPt;
    int m_fps = 60;
    int m_wheelAccum = 0;
    qreal m_scale = 1.0;
    bool m_tabVisible = true;
    bool m_fullscreenAppActive = false;
    bool m_trayIsLight = false;

    AudioDeviceList m_devices;
    QString m_currentDevice;

    friend class StripWindow;
};

// --- StripWindow members that need the presenter's definition --------------

void StripWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The box: our own black behind the bars, a hairline of light around it,
    // and rounded corners only on the sides facing away from the screen edge
    // -- the shape the macOS notch panel draws. The trick for the square
    // shoulders is to push the edge-facing side of the rounded rect out of
    // the window, where its rounding and its stroke are clipped away, leaving
    // flat sides running into the edge.
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

    if (!m_image.isNull()) {
        // At the image's own logical size, centred. The image's pixel count
        // carries the panel scale (it rides in devicePixelRatio()), so
        // dividing by the window's plain DPR yields the scaled-up logical
        // size, rendered at full density rather than stretched.
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
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
    if (!(event->buttons() & Qt::LeftButton))
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

void StripWindow::wheelEvent(QWheelEvent *event)
{
    if (m_owner) {
        m_owner->onStripWheel(event->angleDelta().y());
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void StripWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_owner)
        m_owner->showMenuAtCursor();
    event->accept();
}

bool ThemeWatchFilter::nativeEventFilter(const QByteArray &type, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (type != QByteArrayLiteral("windows_generic_MSG") || !m_owner)
        return false;

    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_SETTINGCHANGE) {
        // lParam names what changed; a light/dark flip broadcasts
        // "ImmersiveColorSet" to every top-level window, ours included. The
        // presenter re-reads the actual state, so over-firing here is cheap.
        const auto *area = reinterpret_cast<const wchar_t *>(msg->lParam);
        if (area && lstrcmpiW(area, L"ImmersiveColorSet") == 0)
            m_owner->onThemeMaybeChanged();
    }
    return false;
}

} // namespace

std::unique_ptr<BarPresenter> BarPresenter::create(QObject *parent)
{
    return std::make_unique<WinBarPresenter>(parent);
}

} // namespace hyperwave
