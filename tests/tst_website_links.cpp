// Links from the app to hypernuclear.com (src/app/WebsiteLinks.h): public
// pages get the app's campaign tags, and nothing that can carry something
// private is touched.
#include "app/WebsiteLinks.h"

#include <QtTest>

using namespace hyperwave;

class TestWebsiteLinks : public QObject
{
    Q_OBJECT
private slots:
    // Tags replace any already there, and everything else the link carried
    // survives: its own query (a promo code, say) and its fragment.
    void tags_replaceOldOnesAndKeepTheRest()
    {
        const QUrl url = websiteLinks::tagged(
            QStringLiteral("http://www.hypernuclear.com/hypershot/purchase/?promo=SAVE&utm_source=old#tiers"),
            QStringLiteral("welcome"));
        QCOMPARE(url.scheme(), QStringLiteral("https"));
        QCOMPARE(url.host(), QStringLiteral("hypernuclear.com"));
        QCOMPARE(url.fragment(), QStringLiteral("tiers"));
        const QUrlQuery q(url);
        QCOMPARE(q.queryItemValue(QStringLiteral("promo")), QStringLiteral("SAVE"));
        QCOMPARE(q.allQueryItemValues(QStringLiteral("utm_source")), QStringList{QStringLiteral("hyperwave")});
        QCOMPARE(q.queryItemValue(QStringLiteral("utm_medium")), QStringLiteral("app"));
        QCOMPARE(q.queryItemValue(QStringLiteral("utm_campaign")), QStringLiteral("welcome"));
    }
    // A path is a page on the site.
    void tags_resolveAPathAgainstTheSite()
    {
        const QUrl url = websiteLinks::tagged(QStringLiteral("/hyperwave/"), QStringLiteral("welcome"));
        QCOMPARE(url.host(), QStringLiteral("hypernuclear.com"));
        QCOMPARE(url.path(), QStringLiteral("/hyperwave/"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("utm_campaign")), QStringLiteral("welcome"));
    }
    // A license key, a machine request, a checkout or another site: as is.
    void tags_leaveAnythingPrivateAlone()
    {
        for (const char *link : {
                 "https://hypernuclear.com/activate?key=secret",
                 "https://www.hypernuclear.com/activate/?key=secret",
                 "https://hypernuclear.com/api/v1/store?source=hyperwave_app",
                 "https://pay.hypernuclear.com/b/x?client_reference_id=hyperwave_app",
                 "https://support.hypernuclear.com/",
                 "https://github.com/hypernuclear/hyperwave",
                 "mailto:support@hypernuclear.com"}) {
            const QString s = QString::fromLatin1(link);
            QCOMPARE(websiteLinks::tagged(s, QStringLiteral("welcome")), QUrl(s));
        }
    }
    // No campaign, no tags: the link is only resolved.
    void tags_needACampaign()
    {
        const QUrl url = websiteLinks::tagged(QStringLiteral("https://hypernuclear.com/"), QString());
        QVERIFY(!url.hasQuery());
    }
};

QTEST_GUILESS_MAIN(TestWebsiteLinks)
#include "tst_website_links.moc"
