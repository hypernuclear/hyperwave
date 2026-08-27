#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

namespace hyperwave {

// What is playing, and the controls for it.
//
// Not MediaRemote. That private framework is still present on macOS 26 and
// every symbol still resolves, but since 15.4 it is gated behind
// com.apple.mediaremote.send-command, an entitlement Apple does not issue to
// third parties: MRMediaRemoteGetNowPlayingInfo fires its callback with an
// empty dictionary while music is demonstrably playing. Verified here rather
// than assumed, because it is the sort of thing that silently half-works.
//
// So this is AppleScript, per application, which has two consequences worth
// being honest about. It needs an Automation grant for each app it talks to,
// which is why it stays off until asked for -- the app's first run is otherwise
// a single clean permission question. And it only covers applications that
// publish a scripting dictionary: Spotify and Music do, browsers do not, so
// audio from a YouTube tab gets a name from the audio process list and nothing
// else.
struct NowPlayingInfo
{
    bool valid = false;
    bool playing = false;
    QString title;
    QString artist;
    QString album;
    QString appName;
    QString bundleId;

    bool operator==(const NowPlayingInfo &other) const
    {
        return valid == other.valid && playing == other.playing && title == other.title
            && artist == other.artist && bundleId == other.bundleId;
    }
    bool operator!=(const NowPlayingInfo &other) const { return !(*this == other); }
};

enum class TransportCommand { PlayPause, Next, Previous };

class NowPlaying : public QObject
{
    Q_OBJECT

public:
    explicit NowPlaying(QObject *parent = nullptr);

    // True where this bundle id has a scripting dictionary we know how to read.
    static bool isSupported(const QString &bundleId);

    // Which app to ask. Empty stops polling entirely.
    void setBundleId(const QString &bundleId);
    QString bundleId() const { return m_bundleId; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    const NowPlayingInfo &info() const { return m_info; }

    void sendCommand(TransportCommand command);

Q_SIGNALS:
    void infoChanged(const NowPlayingInfo &info);

private Q_SLOTS:
    void poll();

private:
    QTimer m_timer;
    QString m_bundleId;
    NowPlayingInfo m_info;
    bool m_enabled = false;
};

} // namespace hyperwave
