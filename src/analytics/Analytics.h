// Usage analytics, on by default and switchable off.
//
// Off is the real thing: the SDK is not initialised, no device ID is computed,
// and no request is made. The switch is in the menu next to everything else the
// app can be told to stop doing, and once somebody turns it off it stays off --
// the default only decides how an install starts.
//
// Anywhere that requires consent before non-essential analytics -- the EU is the
// obvious one -- an opt-out default is not sufficient on its own, and a
// first-run disclosure has to carry the consent instead.
//
// WHAT THIS MUST NEVER SEND, which matters more here than in most apps.
//
// hyperwave can hear everything the machine plays and, when the user turns it
// on, everything the microphone hears. None of that is analytics. There is
// nothing here about audio content, track titles, application names from the
// Playing list, output device names, or the spectrum itself, and there is no
// code path that could carry them: `event()` takes a name and a small map, and
// every call site in this repository passes constants from the namespaces below
// or a bounded enum-like string such as which look is running.
//
// If a future event needs a value that came from the audio system, it does not
// need that event.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace hyperwave {

class Analytics : public QObject
{
    Q_OBJECT

public:
    explicit Analytics(QObject *parent = nullptr) : QObject(parent) {}
    ~Analytics() override = default;

    /// The analytics backend for this build, or null when there is not one --
    /// no app key was compiled in, or analytics was configured out. Null is
    /// normal: it is what every local developer build returns, and callers hide
    /// the menu entry rather than showing a switch that does nothing.
    static Analytics *create(QObject *parent = nullptr);

    /// Whether collection is on. Persisted, and on unless turned off.
    virtual bool enabled() const = 0;

    /// Turn collection on or off. Turning it on initialises the SDK on first
    /// use and starts a session; turning it off ends the session and stops
    /// everything.
    virtual void setEnabled(bool on) = 0;

    /// Record a named event. A no-op while disabled.
    virtual void event(const QString &name, const QVariantMap &segmentation = {}) = 0;

    /// Set a property describing this install (platform, version, and so on).
    /// A no-op while disabled.
    virtual void property(const QString &key, const QString &value) = 0;

Q_SIGNALS:
    void enabledChanged(bool on);
};

/// The event names, in one place.
///
/// Spelled out as constants rather than string literals at each call site: a
/// typo in an event name does not fail, it silently creates a second event that
/// looks almost like the first, and the mistake only surfaces weeks later in a
/// dashboard nobody can reconcile.
namespace Ev {
inline constexpr auto kLaunch = "launch";
inline constexpr auto kLook = "look_changed";
inline constexpr auto kGradient = "gradient_changed";
inline constexpr auto kInput = "input_toggled";
inline constexpr auto kGlow = "glow_toggled";
inline constexpr auto kCompact = "compact_toggled";
inline constexpr auto kSettings = "settings_opened";
inline constexpr auto kSplashShown = "splash_shown";
inline constexpr auto kPermission = "permission_granted";
// Whether a source OTHER than the whole system mix is in use -- not which one.
// The device's name is the user's business and never leaves the machine.
inline constexpr auto kSourcePicked = "source_picked";
} // namespace Ev

/// User-property keys, same reasoning as the event names.
namespace Prop {
inline constexpr auto kVersion = "app_version";
inline constexpr auto kOs = "os";
inline constexpr auto kOsVersion = "os_version";
inline constexpr auto kLook = "look";
inline constexpr auto kGradient = "gradient";
// Whether this Mac has a camera housing, which decides whether the notch panel
// exists at all. One bit, and it explains most of what the panel's numbers say.
inline constexpr auto kNotch = "notch";
} // namespace Prop

} // namespace hyperwave
