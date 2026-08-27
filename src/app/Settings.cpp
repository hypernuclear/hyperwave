#include "app/Settings.h"

#include "render/Palette.h"

namespace hyperwave {

namespace {
constexpr const char *kGradientKey = "appearance/gradient";
constexpr const char *kVizKey = "appearance/visualiser";
constexpr const char *kCompactKey = "appearance/compact";
constexpr const char *kHalfRateKey = "power/halfRateOnBattery";
constexpr const char *kSourceKey = "audio/sourceDeviceUid";
constexpr const char *kNowPlayingKey = "nowPlaying/enabled";
constexpr const char *kNotchAutoHideKey = "notch/autoHide";
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

QString Settings::vizStyle() const
{
    return m_store.value(QLatin1String(kVizKey), QStringLiteral("bars")).toString();
}
void Settings::setVizStyle(const QString &id)
{
    if (vizStyle() == id)
        return;
    m_store.setValue(QLatin1String(kVizKey), id);
    Q_EMIT vizStyleChanged(id);
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
QString Settings::sourceDeviceUid() const
{
    return m_store.value(QLatin1String(kSourceKey), QString()).toString();
}
void Settings::setSourceDeviceUid(const QString &uid)
{
    if (sourceDeviceUid() == uid)
        return;
    m_store.setValue(QLatin1String(kSourceKey), uid);
    Q_EMIT sourceDeviceUidChanged(uid);
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
bool Settings::notchAutoHide() const
{
    return m_store.value(QLatin1String(kNotchAutoHideKey), true).toBool();
}
void Settings::setNotchAutoHide(bool enabled)
{
    if (notchAutoHide() == enabled)
        return;
    m_store.setValue(QLatin1String(kNotchAutoHideKey), enabled);
    Q_EMIT notchAutoHideChanged(enabled);
}

} // namespace hyperwave
