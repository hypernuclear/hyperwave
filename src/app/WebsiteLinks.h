// Links from the app to hypernuclear.com, tagged so the site's analytics
// can tell the visit came from here: utm_source=hyperwave, utm_medium=app,
// utm_campaign=<the place in the app>. The same rules as hypershot's
// src/analytics/website_links.h, so the three apps' visits read alike.
//
// Only public pages on the main site are tagged. Anything that can carry
// something private is left exactly as it was — /activate (a license key),
// /api/ (machine requests), and every other host (the store's checkout on
// pay.hypernuclear.com, the support portal) — and a link is never recorded,
// only the place in the app it was opened from.
#pragma once

#include <QString>
#include <QUrl>
#include <QUrlQuery>

namespace hyperwave::websiteLinks {

inline constexpr auto kHost   = "hypernuclear.com";
inline constexpr auto kSource = "hyperwave";

/// Whether `url` is a public page on hypernuclear.com, the only kind tagged.
inline bool isWebsite(const QUrl &url)
{
    const QString host = QString::fromLatin1(kHost);
    return (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
        && (url.host() == host || url.host() == QLatin1String("www.") + host)
        && url.path() != QLatin1String("/activate")
        && !url.path().startsWith(QLatin1String("/activate/"))
        && !url.path().startsWith(QLatin1String("/api/"));
}

/// `link` (absolute, or a path on the site) as it should be opened: a
/// website page gets this app's campaign tags, replacing any it had;
/// anything else comes back untouched.
inline QUrl tagged(const QString &link, const QString &campaign)
{
    QUrl url(link);
    if (url.isRelative())
        url = QUrl(QStringLiteral("https://%1/").arg(QLatin1String(kHost))).resolved(url);
    if (!isWebsite(url) || campaign.isEmpty())
        return url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(QString::fromLatin1(kHost));
    QUrlQuery query(url);
    for (const char *key : {"utm_source", "utm_medium", "utm_campaign"})
        query.removeAllQueryItems(QString::fromLatin1(key));
    query.addQueryItem(QStringLiteral("utm_source"), QString::fromLatin1(kSource));
    query.addQueryItem(QStringLiteral("utm_medium"), QStringLiteral("app"));
    query.addQueryItem(QStringLiteral("utm_campaign"), campaign);
    url.setQuery(query);
    return url;
}

} // namespace hyperwave::websiteLinks
