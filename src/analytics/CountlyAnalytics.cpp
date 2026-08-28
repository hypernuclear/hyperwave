#include "analytics/CountlyAnalytics.h"
#include "analytics/MachineId.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>

#include <countly.hpp>

namespace hyperwave {

CountlyAnalytics::CountlyAnalytics(const QString &appKey, const QString &serverUrl,
                                   QObject *parent)
    : Analytics(parent)
    , m_appKey(appKey)
    , m_serverUrl(serverUrl)
    , m_store(QStringLiteral("Hypernuclear"), QStringLiteral("hyperwave"))
{
    // On by default, decided by the absence of a stored value rather than by
    // any first-run code that might not run. The switch writes through to this
    // key, so anyone who turns it off stays off.
    m_enabled.store(m_store.value(QStringLiteral("analyticsEnabled"), true).toBool());

    // Bring the SDK up here when collection is already on.
    //
    // setEnabled() cannot do it: it returns early when the value has not
    // changed, and on a normal launch it has not. The result would be an app
    // that reads "on" in the menu, records every event, and drops all of them
    // at the !m_initialised check -- a state that looks correct from the
    // outside and produces no data at all.
    if (m_enabled.load()) {
        initialise();
        cly::Countly::getInstance().beginSession();
    }
}

CountlyAnalytics::~CountlyAnalytics()
{
    if (m_enabled.load() && m_initialised) {
        cly::Countly::getInstance().endSession();
        cly::Countly::getInstance().stop();
    }
}

bool CountlyAnalytics::enabled() const { return m_enabled.load(); }

void CountlyAnalytics::setEnabled(bool on)
{
    QMutexLocker lock(&m_mutex);
    if (m_enabled.load() == on)
        return;

    if (on) {
        if (!m_initialised)
            initialise();
        m_enabled.store(true);
        cly::Countly::getInstance().beginSession();
    } else {
        m_enabled.store(false);
        if (m_initialised)
            cly::Countly::getInstance().endSession();
    }
    m_store.setValue(QStringLiteral("analyticsEnabled"), on);
    // Flushed now: an opt-OUT lost because the process was killed before its
    // preferences were written is the one failure here that actually matters.
    m_store.sync();
    Q_EMIT enabledChanged(on);
}

void CountlyAnalytics::event(const QString &name, const QVariantMap &segmentation)
{
    if (!m_enabled.load() || !m_initialised)
        return;
    if (segmentation.isEmpty()) {
        cly::Countly::getInstance().RecordEvent(name.toStdString(), 1);
        return;
    }
    std::map<std::string, std::string> seg;
    for (auto it = segmentation.constBegin(); it != segmentation.constEnd(); ++it)
        seg[it.key().toStdString()] = it.value().toString().toStdString();
    cly::Countly::getInstance().RecordEvent(name.toStdString(), seg, 1);
}

void CountlyAnalytics::property(const QString &key, const QString &value)
{
    if (!m_enabled.load() || !m_initialised)
        return;
    cly::Countly::getInstance().setCustomUserDetails({{key.toStdString(), value.toStdString()}});
}

void CountlyAnalytics::initialise()
{
    auto &c = cly::Countly::getInstance();
    c.setDeviceID(machineId().toStdString());
    // Sessions are ours to open and close, not the SDK's to infer.
    c.enableManualSessionControl();

    // Qt's SHA-256 and Qt's networking, rather than the SDK's OpenSSL and
    // libcurl defaults. Both are already linked here, and neither wants to be a
    // second copy inside a menu bar app.
    c.setSha256([](const std::string &in) {
        return QCryptographicHash::hash(QByteArray::fromRawData(in.data(), qsizetype(in.size())),
                                        QCryptographicHash::Sha256)
            .toHex()
            .toStdString();
    });

    const QString server = m_serverUrl;
    c.setHTTPClient([server](bool post, const std::string &path,
                             const std::string &data) -> cly::HTTPResponse {
        cly::HTTPResponse r;
        r.success = false;
        // Called from the SDK's own thread, which can outlive the application
        // object during shutdown. QNetworkAccessManager needs a live one.
        if (!QCoreApplication::instance())
            return r;

        QString url = server + QString::fromStdString(path);
        if (!post)
            url += QLatin1Char('?') + QString::fromStdString(data);

        // Built here rather than kept as a member: this runs on the SDK's
        // thread, and a QNetworkAccessManager belongs to the thread that
        // created it.
        QNetworkAccessManager nam;
        QNetworkRequest req{QUrl(url)};
        QNetworkReply *reply = nullptr;
        if (post) {
            req.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
            reply = nam.post(req, QByteArray::fromStdString(data));
        } else {
            reply = nam.get(req);
        }

        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            r.success = true;
            try {
                r.data = nlohmann::json::parse(reply->readAll().toStdString());
            } catch (...) {
                // Not every endpoint answers with JSON, and the request still
                // succeeded.
                r.data = nlohmann::json::object();
            }
        }
        reply->deleteLater();
        return r;
    });

    c.setMetrics(QSysInfo::productType().toStdString(),
                 QSysInfo::productVersion().toStdString(),
                 QSysInfo::prettyProductName().toStdString(), "", "",
                 QCoreApplication::applicationVersion().toStdString());
    c.alwaysUsePost(true);
    c.setUpdateInterval(30000);
    // Without this the worker sleeps out the whole update interval and quitting
    // blocks behind it -- thirty seconds of a menu bar app refusing to close.
    c.enableImmediateRequestOnStop();
    c.start(m_appKey.toStdString(), m_serverUrl.toStdString(), 443, true);
    m_initialised = true;
}

Analytics *Analytics::create(QObject *parent)
{
#ifdef HYPERWAVE_COUNTLY_APP_KEY
    const QString key = QStringLiteral(HYPERWAVE_COUNTLY_APP_KEY);
    if (!key.isEmpty())
        return new CountlyAnalytics(key, QStringLiteral(HYPERWAVE_COUNTLY_SERVER), parent);
#endif
    // No key compiled in -- a local build, or a fork. Nothing to send to.
    Q_UNUSED(parent);
    return nullptr;
}

} // namespace hyperwave
