// What qml/Welcome.qml needs from C++: the usage-data switch it shows (the
// same one as the menu's Send Anonymous Usage Data), the app's version for the
// store request, the "Show at startup" and Open at Login settings, the capture
// line the old About box showed, local copies of the store's product loops,
// and a way to open website links tagged and counted (WebsiteLinks.h).
// Published as the `welcome` context property.
#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;

namespace hyperwave {

class Analytics;
class Settings;

class WelcomeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool analyticsAvailable READ analyticsAvailable NOTIFY analyticsChanged)
    Q_PROPERTY(bool analyticsEnabled READ analyticsEnabled WRITE setAnalyticsEnabled NOTIFY analyticsChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString captureStatus READ captureStatus NOTIFY captureStatusChanged)
    // The window's "Show at startup" box: Settings::welcomeAtStartup, the same
    // switch as the menu's Show Welcome at Startup.
    Q_PROPERTY(bool showAtStartup READ showAtStartup WRITE setShowAtStartup NOTIFY showAtStartupChanged)
    // The menu's Open at Login: the system's login item (LaunchAtLogin.h), read
    // from the system every time, never cached. Unavailable where the platform
    // has no implementation, and the window hides the switch.
    Q_PROPERTY(bool openAtLoginAvailable READ openAtLoginAvailable CONSTANT)
    Q_PROPERTY(bool openAtLogin READ openAtLogin WRITE setOpenAtLogin NOTIFY openAtLoginChanged)
public:
    explicit WelcomeBridge(QObject *parent = nullptr);

    /// The analytics backend; null on a build without one.
    void setAnalytics(Analytics *analytics);
    /// Where showAtStartup is kept. Without one the box reads ticked and
    /// changes nothing.
    void setSettings(Settings *settings);

    bool analyticsAvailable() const { return m_analytics; }
    bool analyticsEnabled() const;
    void setAnalyticsEnabled(bool on);
    QString version() const;
    bool showAtStartup() const;
    void setShowAtStartup(bool on);
    bool openAtLoginAvailable() const;
    bool openAtLogin() const;
    /// Asks the system, then reports what it actually did: registering can
    /// fail, and the switch shows the result, not the request.
    void setOpenAtLogin(bool on);
    /// Re-read the login item: it was changed in the menu, or may have been
    /// in System Settings while the window was closed.
    void refreshOpenAtLogin() { Q_EMIT openAtLoginChanged(); }

    /// "Capture: ... · Rate: ... Hz", refreshed by main() each time the window
    /// opens. The first thing to check when the bars stay flat.
    QString captureStatus() const { return m_captureStatus; }
    void setCaptureStatus(const QString &status);

    /// A local copy of a product loop from the site, as a file:// URL, or ""
    /// until the first download lands (`fileCached` then says where).
    ///
    /// A copy because Qt can't loop an animation it streams: QMovie needs to
    /// rewind, a network reply can't, so a loop played from its URL plays
    /// once and stops. From a file it loops. The copy is checked against the
    /// site once per run; a changed file is saved under a new name and
    /// announced, so a cached loop is served at once and never goes stale
    /// for more than a run. Only https URLs on hypernuclear.com are fetched.
    Q_INVOKABLE QString cachedFile(const QString &url);

    /// Opens `link` in the browser, tagged for `campaign` when it is a page
    /// on the site, and counts the open (campaign only) when it was tagged.
    Q_INVOKABLE void openLink(const QString &link, const QString &campaign);
    /// The sign-up was accepted by the site (HTTP 202). No address.
    Q_INVOKABLE void newsletterSubmitted();
    /// A product card was opened: its app id, nothing else.
    Q_INVOKABLE void promoClicked(const QString &product);
    /// The window was shown, as "first_run", "startup" or "menu".
    void shown(const QString &how);

Q_SIGNALS:
    void analyticsChanged();
    void captureStatusChanged();
    void showAtStartupChanged();
    void openAtLoginChanged();
    void fileCached(const QString &url, const QString &localUrl);

private:
    void event(const char *name, const QVariantMap &segmentation = {});
    void refresh(const QString &url, const QString &current);
    QPointer<Analytics> m_analytics;
    QPointer<Settings> m_settings;
    QString m_captureStatus;
    QNetworkAccessManager *m_net = nullptr;
    QSet<QString> m_checked;   // URLs already revalidated this run
};

} // namespace hyperwave
