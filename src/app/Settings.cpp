#include "app/Settings.h"

#include "render/Palette.h"

namespace hyperlight {

namespace {
constexpr const char *kGradientKey = "appearance/gradient";
constexpr const char *kCompactKey = "appearance/compact";
constexpr const char *kHalfRateKey = "power/halfRateOnBattery";
constexpr const char *kSourceKey = "audio/sourceBundleId";
constexpr const char *kNowPlayingKey = "nowPlaying/enabled";
} // namespace

Settings::Settings(QObject *parent, const QString &application)
    : QObject(parent)
    , m_store(QStringLiteral("Hypernuclear"), application)
{
}

QString Settings::gradientId() const
{
    return m_store.value(QLatin1String(kGradientKey), Palette::defaultId()).toString();
}

void Settings::setGradientId(const QString &id)
{
    if (gradientId() == id)
        return;
    m_store.setValue(QLatin1String(kGradientKey), id);
    Q_EMIT gradientIdChanged(id);
}

bool Settings::compact() const
{
    return m_store.value(QLatin1String(kCompactKey), false).toBool();
}

void Settings::setCompact(bool compact)
{
    if (this->compact() == compact)
        return;
    m_store.setValue(QLatin1String(kCompactKey), compact);
    Q_EMIT compactChanged(compact);
}

bool Settings::halfRateOnBattery() const
{
    return m_store.value(QLatin1String(kHalfRateKey), true).toBool();
}

void Settings::setHalfRateOnBattery(bool enabled)
{
    m_store.setValue(QLatin1String(kHalfRateKey), enabled);
}
QString Settings::sourceBundleId() const
{
    return m_store.value(QLatin1String(kSourceKey), QString()).toString();
}
void Settings::setSourceBundleId(const QString &bundleId)
{
    if (sourceBundleId() == bundleId)
        return;
    m_store.setValue(QLatin1String(kSourceKey), bundleId);
    Q_EMIT sourceBundleIdChanged(bundleId);
}
bool Settings::nowPlayingEnabled() const
{
    return m_store.value(QLatin1String(kNowPlayingKey), false).toBool();
}
void Settings::setNowPlayingEnabled(bool enabled)
{
    if (nowPlayingEnabled() == enabled)
        return;
    m_store.setValue(QLatin1String(kNowPlayingKey), enabled);
    Q_EMIT nowPlayingEnabledChanged(enabled);
}

} // namespace hyperlight
