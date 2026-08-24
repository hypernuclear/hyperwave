#include "app/Settings.h"

#include "render/Palette.h"

namespace hyperlight {

namespace {
constexpr const char *kGradientKey = "appearance/gradient";
constexpr const char *kCompactKey = "appearance/compact";
constexpr const char *kHalfRateKey = "power/halfRateOnBattery";
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

} // namespace hyperlight
