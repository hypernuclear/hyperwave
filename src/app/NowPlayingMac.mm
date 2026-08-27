#include "app/NowPlaying.h"

#import <AppKit/AppKit.h>

#include <QDebug>

namespace hyperwave {

namespace {

// Poll rather than subscribe. Both Spotify and Music post distributed
// notifications on track change, but they are unreliable across versions and
// arrive before the app has updated its own scripting state, so a poll that is
// slow enough to be free is both simpler and more accurate.
constexpr int kPollIntervalMs = 2000;

struct ScriptableApp
{
    const char *bundleId;
    const char *appName;   // the name AppleScript addresses it by
};

// Only applications with a scripting dictionary that exposes a current track.
// Browsers are the conspicuous absence and there is no way around it: a YouTube
// tab is not something Chrome will tell anyone about.
constexpr ScriptableApp kSupported[] = {
    {"com.spotify.client", "Spotify"},
    {"com.apple.Music", "Music"},
    {"com.apple.iTunes", "iTunes"},
};

const ScriptableApp *lookup(const QString &bundleId)
{
    for (const ScriptableApp &app : kSupported) {
        if (bundleId == QLatin1String(app.bundleId))
            return &app;
    }
    return nullptr;
}

bool isRunning(const QString &bundleId)
{
    NSString *wanted = bundleId.toNSString();
    for (NSRunningApplication *app in [[NSWorkspace sharedWorkspace] runningApplications]) {
        if ([app.bundleIdentifier isEqualToString:wanted])
            return true;
    }
    return false;
}

// Runs a script and returns its result, or a null QString on any failure.
//
// Never prompts by itself: the Automation grant is requested by the first
// script that actually targets another app, which is why nothing here runs
// until the feature is switched on.
QString runScript(NSString *source)
{
    NSDictionary *error = nil;
    NSAppleScript *script = [[NSAppleScript alloc] initWithSource:source];
    NSAppleEventDescriptor *result = [script executeAndReturnError:&error];
    if (error) {
        static bool complained = false;
        if (!complained) {
            complained = true;
            qWarning("hyperwave: now playing unavailable (%s)",
                     [[error description] UTF8String]);
        }
        return {};
    }
    return result.stringValue ? QString::fromNSString(result.stringValue) : QString();
}

} // namespace

NowPlaying::NowPlaying(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &NowPlaying::poll);
}

bool NowPlaying::isSupported(const QString &bundleId)
{
    return lookup(bundleId) != nullptr;
}

void NowPlaying::setBundleId(const QString &bundleId)
{
    if (m_bundleId == bundleId)
        return;
    m_bundleId = bundleId;
    m_info = {};
    Q_EMIT infoChanged(m_info);

    if (m_enabled && isSupported(m_bundleId)) {
        poll();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

void NowPlaying::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;

    if (m_enabled && isSupported(m_bundleId)) {
        poll();
        m_timer.start();
    } else {
        m_timer.stop();
        m_info = {};
        Q_EMIT infoChanged(m_info);
    }
}

void NowPlaying::poll()
{
    const ScriptableApp *app = lookup(m_bundleId);
    if (!app || !m_enabled)
        return;

    // Not running is a normal state, not a failure -- and asking a stopped app
    // for its current track is what launches it, which would be a rude thing
    // for a visualiser to do.
    if (!isRunning(m_bundleId)) {
        if (m_info.valid) {
            m_info = {};
            Q_EMIT infoChanged(m_info);
        }
        return;
    }

    // The fields come back as one delimited string because three round trips
    // through the Apple Event machinery cost three times as much as one.
    NSString *source = [NSString
        stringWithFormat:@"tell application \"%s\"\n"
                          "  if player state is stopped then return \"\"\n"
                          "  set t to name of current track\n"
                          "  set a to artist of current track\n"
                          "  set b to album of current track\n"
                          "  set s to (player state as text)\n"
                          "  return t & \"\\t\" & a & \"\\t\" & b & \"\\t\" & s\n"
                          "end tell",
                         app->appName];

    const QString raw = runScript(source);

    NowPlayingInfo info;
    const QStringList parts = raw.split(QLatin1Char('\t'));
    if (parts.size() >= 4) {
        info.valid = true;
        info.title = parts[0];
        info.artist = parts[1];
        info.album = parts[2];
        info.playing = parts[3].compare(QLatin1String("playing"), Qt::CaseInsensitive) == 0;
        info.bundleId = m_bundleId;
        info.appName = QString::fromUtf8(app->appName);
    }

    if (info != m_info) {
        m_info = info;
        Q_EMIT infoChanged(m_info);
    }
}

void NowPlaying::sendCommand(TransportCommand command)
{
    const ScriptableApp *app = lookup(m_bundleId);
    if (!app || !m_enabled || !isRunning(m_bundleId))
        return;

    const char *verb = "playpause";
    switch (command) {
    case TransportCommand::PlayPause: verb = "playpause"; break;
    case TransportCommand::Next: verb = "next track"; break;
    case TransportCommand::Previous: verb = "previous track"; break;
    }

    runScript([NSString stringWithFormat:@"tell application \"%s\" to %s", app->appName, verb]);

    // The app updates its own state a beat after acknowledging the command, so
    // re-reading immediately would report what was true a moment ago.
    QTimer::singleShot(250, this, &NowPlaying::poll);
}

} // namespace hyperwave
