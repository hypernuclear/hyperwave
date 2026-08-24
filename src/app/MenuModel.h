#pragma once

#include <QString>
#include <QVector>

namespace hyperlight {

// A platform-free description of a menu.
//
// The content of the menu -- which apps are playing, which gradient is active,
// whether the audio permission needs asking for -- is the same on every
// platform; only the widget that renders it differs. Keeping the model here
// means the eventual Windows tray menu is a rendering job rather than a
// reimplementation, and it keeps AppKit types out of App.
struct MenuItem
{
    enum class Kind { Action, Separator, Submenu };

    Kind kind = Kind::Action;
    QString title;
    QString actionId;    // reported back when chosen; empty for non-actions
    QString detail;      // dimmed trailing text, e.g. "playing"
    QString iconBundleId;// draw this app's icon beside the title, if it has one
    bool checked = false;
    bool enabled = true;
    QVector<MenuItem> children;

    static MenuItem separator()
    {
        MenuItem item;
        item.kind = Kind::Separator;
        return item;
    }

    static MenuItem action(const QString &actionId, const QString &title)
    {
        MenuItem item;
        item.actionId = actionId;
        item.title = title;
        return item;
    }

    static MenuItem submenu(const QString &title, QVector<MenuItem> children)
    {
        MenuItem item;
        item.kind = Kind::Submenu;
        item.title = title;
        item.children = std::move(children);
        return item;
    }

    MenuItem &withCheck(bool on)
    {
        checked = on;
        return *this;
    }
    MenuItem &withDetail(const QString &text)
    {
        detail = text;
        return *this;
    }
    MenuItem &withIcon(const QString &bundleId)
    {
        iconBundleId = bundleId;
        return *this;
    }
    MenuItem &withEnabled(bool on)
    {
        enabled = on;
        return *this;
    }
};

using MenuModel = QVector<MenuItem>;

// Action ids. Strings rather than an enum so the platform layer can pass them
// straight through without needing to know what any of them mean; the source
// picker in particular generates ids at runtime from bundle ids.
namespace MenuAction {
inline constexpr const char *kSourcePrefix = "source:";   // + bundle id, empty for all
inline constexpr const char *kGradientPrefix = "gradient:";
inline constexpr const char *kToggleCompact = "toggle-compact";
inline constexpr const char *kToggleNowPlaying = "toggle-now-playing";
inline constexpr const char *kToggleNotch = "toggle-notch";
inline constexpr const char *kRequestPermission = "request-permission";
inline constexpr const char *kRestart = "restart";
inline constexpr const char *kAbout = "about";
inline constexpr const char *kQuit = "quit";
inline constexpr const char *kToggleAutoHide = "toggle-auto-hide";
inline constexpr const char *kTransportPlayPause = "transport-playpause";
inline constexpr const char *kTransportNext = "transport-next";
inline constexpr const char *kTransportPrevious = "transport-previous";
} // namespace MenuAction

} // namespace hyperlight
