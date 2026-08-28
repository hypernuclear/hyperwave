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
#include <QWindow>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
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

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    WinBarPresenter *m_owner = nullptr;
    QImage m_image;
    QString m_nowPlaying;
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
        m_window->resize(m_widthPt, kDefaultHeightPt);
        positionFloating();
        m_window->show();

        m_filter = std::make_unique<AppBarFilter>(this);
        if (QCoreApplication *app = QCoreApplication::instance())
            app->installNativeEventFilter(m_filter.get());

        installTray();

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
        if (m_docked)
            applyDockedGeometry();
        else
            m_window->resize(m_widthPt, kDefaultHeightPt);
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

    // Docking is this platform's business, not the app's, so these items are
    // added here rather than in the shared MenuModel.
    void appendWindowsItems(QMenu *menu)
    {
        menu->addSeparator();

        QAction *dock = menu->addAction(tr("Dock to screen edge"));
        dock->setCheckable(true);
        dock->setChecked(m_docked);
        QObject::connect(dock, &QAction::triggered, this, [this](bool on) {
            if (on)
                dockTo(m_edge);
            else
                undock();
        });

        QMenu *edges = menu->addMenu(tr("Dock edge"));
        struct EdgeChoice
        {
            UINT edge;
            const char *label;
        };
        static const EdgeChoice kEdges[] = {
            {ABE_TOP, "Top"},
            {ABE_BOTTOM, "Bottom"},
        };
        for (const EdgeChoice &choice : kEdges) {
            QAction *action = edges->addAction(QString::fromLatin1(choice.label));
            action->setCheckable(true);
            action->setChecked(m_docked && m_edge == choice.edge);
            const UINT edge = choice.edge;
            QObject::connect(action, &QAction::triggered, this, [this, edge] { dockTo(edge); });
        }
    }

    void installTray()
    {
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            return;

        m_tray = std::make_unique<QSystemTrayIcon>();
        m_tray->setIcon(staticIcon());
        m_tray->setToolTip(QStringLiteral("hyperwave"));

        // No setContextMenu: Qt would pop the menu itself, before the app has
        // had a chance to rebuild it. Handling activation ourselves keeps the
        // refresh-then-show ordering.
        QObject::connect(m_tray.get(), &QSystemTrayIcon::activated, this,
                         [this](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Context)
                                 showMenuAtCursor();
                             else if (reason == QSystemTrayIcon::Trigger)
                                 emitClicked();
                         });
        m_tray->show();
    }

    // Drawn once. Deliberately not the visualiser: at this size twenty-eight
    // bands is eight bars of three pixels, and Shell_NotifyIcon is a
    // cross-process call into explorer for every update.
    static QIcon staticIcon()
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xff, 0xff, 0xff));
        static const int heights[] = {10, 20, 14, 26, 16};
        for (int i = 0; i < 5; ++i) {
            const int h = heights[i];
            painter.drawRect(3 + i * 6, 30 - h, 4, h);
        }
        painter.end();
        return QIcon(pixmap);
    }

    // --- geometry ---------------------------------------------------------

    void positionFloating()
    {
        if (!m_window)
            return;
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        // Just above the taskbar, centred. availableGeometry already excludes
        // the taskbar and any appbar, including ours.
        const QRect available = screen->availableGeometry();
        m_window->setGeometry(available.center().x() - m_widthPt / 2,
                              available.bottom() - kDefaultHeightPt - 8, m_widthPt,
                              kDefaultHeightPt);
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
        positionFloating();
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

    int m_widthPt = kDefaultWidthPt;
    int m_fps = 60;
    bool m_docked = false;
    bool m_tabVisible = true;
    bool m_fullscreenAppActive = false;
    UINT m_edge = ABE_TOP;

    AudioDeviceList m_devices;
    QString m_currentDevice;

    friend class StripWindow;
};

// --- StripWindow members that need the presenter's definition --------------

void StripWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!m_image.isNull()) {
        // The renderer is asked for exactly this strip's size, so this is a
        // blit rather than a rescale in the normal case; the rect form only
        // matters during a resize, between the geometry change and the next
        // rendered frame.
        painter.drawImage(rect(), m_image);
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
    if (event->button() == Qt::LeftButton && m_owner)
        m_owner->emitClicked();
    QWidget::mousePressEvent(event);
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
    return false;
}

} // namespace

std::unique_ptr<BarPresenter> BarPresenter::create(QObject *parent)
{
    return std::make_unique<WinBarPresenter>(parent);
}

} // namespace hyperwave
