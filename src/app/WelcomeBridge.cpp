#include "app/WelcomeBridge.h"

#include "analytics/Analytics.h"
#include "app/Settings.h"
#include "app/WebsiteLinks.h"
#include "platform/LaunchAtLogin.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace hyperwave {

WelcomeBridge::WelcomeBridge(QObject *parent) : QObject(parent) {}

void WelcomeBridge::setAnalytics(Analytics *analytics)
{
    m_analytics = analytics;
    if (analytics)
        connect(analytics, &Analytics::enabledChanged, this, &WelcomeBridge::analyticsChanged);
    Q_EMIT analyticsChanged();
}

void WelcomeBridge::setSettings(Settings *settings)
{
    m_settings = settings;
    if (settings)
        connect(settings, &Settings::welcomeAtStartupChanged, this,
                &WelcomeBridge::showAtStartupChanged);
    Q_EMIT showAtStartupChanged();
}

bool WelcomeBridge::analyticsEnabled() const
{
    return m_analytics && m_analytics->enabled();
}

void WelcomeBridge::setAnalyticsEnabled(bool on)
{
    if (m_analytics && m_analytics->enabled() != on)
        m_analytics->setEnabled(on);
}

QString WelcomeBridge::version() const
{
    return QStringLiteral(HYPERWAVE_VERSION);
}

bool WelcomeBridge::showAtStartup() const
{
    return m_settings ? m_settings->welcomeAtStartup() : true;
}

void WelcomeBridge::setShowAtStartup(bool on)
{
    if (m_settings)
        m_settings->setWelcomeAtStartup(on);
}

bool WelcomeBridge::openAtLoginAvailable() const
{
    return launchAtLogin::supported();
}

bool WelcomeBridge::openAtLogin() const
{
    return launchAtLogin::supported() && launchAtLogin::isEnabled();
}

void WelcomeBridge::setOpenAtLogin(bool on)
{
    if (!launchAtLogin::supported())
        return;
    const bool actual = launchAtLogin::setEnabled(on);
    // Counted like the menu's switch: what the system did, not the request.
    event(Ev::kLoginItem, {{QStringLiteral("on"), actual ? "1" : "0"}});
    Q_EMIT openAtLoginChanged();
}

void WelcomeBridge::setCaptureStatus(const QString &status)
{
    if (status == m_captureStatus)
        return;
    m_captureStatus = status;
    Q_EMIT captureStatusChanged();
}

namespace {

// Loops are small; anything past this isn't one.
constexpr qint64 kMaxLoopBytes = 8 * 1024 * 1024;

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/store");
}

// A cached copy is `<url hash>-<body hash>.<ext>`: the URL's part finds it,
// the body's part changes when the file does.
QString urlKey(const QString &url)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

QString currentCopy(const QString &url)
{
    const QStringList found = QDir(cacheDir()).entryList({urlKey(url) + QStringLiteral("-*")},
                                                         QDir::Files);
    return found.isEmpty() ? QString() : cacheDir() + QLatin1Char('/') + found.first();
}

bool fetchable(const QUrl &url)
{
    const QString host = url.host().toLower();
    return url.scheme() == QLatin1String("https")
        && (host == QLatin1String("hypernuclear.com")
            || host.endsWith(QLatin1String(".hypernuclear.com")));
}

} // namespace

QString WelcomeBridge::cachedFile(const QString &url)
{
    if (!fetchable(QUrl(url)))
        return {};
    const QString current = currentCopy(url);
    if (!m_checked.contains(url)) {
        m_checked.insert(url);
        refresh(url, current);
    }
    return current.isEmpty() ? QString() : QUrl::fromLocalFile(current).toString();
}

void WelcomeBridge::refresh(const QString &url, const QString &current)
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, current] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;   // offline: the copy we have (if any) stands
        const QByteArray body = reply->readAll();
        if (body.isEmpty() || body.size() > kMaxLoopBytes)
            return;
        const QString ext = QFileInfo(QUrl(url).path()).suffix();
        const QString path = cacheDir() + QLatin1Char('/') + urlKey(url) + QLatin1Char('-')
            + QString::fromLatin1(
                QCryptographicHash::hash(body, QCryptographicHash::Sha1).toHex().left(12))
            + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
        if (path == current)
            return;   // unchanged
        QDir().mkpath(cacheDir());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit())
            return;
        if (!current.isEmpty())
            QFile::remove(current);
        Q_EMIT fileCached(url, QUrl::fromLocalFile(path).toString());
    });
}

void WelcomeBridge::openLink(const QString &link, const QString &campaign)
{
    const QUrl url = websiteLinks::tagged(link, campaign);
    if (QDesktopServices::openUrl(url) && websiteLinks::isWebsite(url) && !campaign.isEmpty())
        event(Ev::kLinkOpened, {{QStringLiteral("source"), campaign}});
}

void WelcomeBridge::newsletterSubmitted()
{
    event(Ev::kNewsletter);
}

void WelcomeBridge::promoClicked(const QString &product)
{
    event(Ev::kPromoClicked, {{QStringLiteral("product"), product}});
}

void WelcomeBridge::shown(const QString &how)
{
    event(Ev::kWelcomeShown, {{QStringLiteral("mode"), how}});
}

// A no-op while analytics is off: the service checks, as it does for every
// other event.
void WelcomeBridge::event(const char *name, const QVariantMap &segmentation)
{
    if (m_analytics)
        m_analytics->event(QString::fromLatin1(name), segmentation);
}

} // namespace hyperwave
