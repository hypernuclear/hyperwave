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
// Visualiser parameters, one key each.
//
// The defaults come from a freshly constructed RenderStyle rather than from
// literals here, so the values tuned in the header stay the single source of
// truth and a stored file that predates a new parameter simply picks it up.
RenderStyle::Strands Settings::strands() const
{
    const RenderStyle defaults;
    RenderStyle::Strands s = defaults.strands;
    const auto num = [this](const char *key, qreal fallback) {
        return m_store.value(QLatin1String(key), fallback).toDouble();
    };
    s.count = m_store.value(QStringLiteral("strands/count"), s.count).toInt();
    s.radius = num("strands/radius", s.radius);
    s.thicknessPt = num("strands/thicknessPt", s.thicknessPt);
    s.fill = num("strands/fill", s.fill);
    s.tiltDegrees = num("strands/tiltDegrees", s.tiltDegrees);
    s.twists = num("strands/twists", s.twists);
    s.spinRate = num("strands/spinRate", s.spinRate);
    s.opacity = num("strands/opacity", s.opacity);
    s.audioGain = num("strands/audioGain", s.audioGain);
    return s;
}

void Settings::setStrands(const RenderStyle::Strands &s)
{
    m_store.setValue(QStringLiteral("strands/count"), s.count);
    m_store.setValue(QStringLiteral("strands/radius"), s.radius);
    m_store.setValue(QStringLiteral("strands/thicknessPt"), s.thicknessPt);
    m_store.setValue(QStringLiteral("strands/fill"), s.fill);
    m_store.setValue(QStringLiteral("strands/tiltDegrees"), s.tiltDegrees);
    m_store.setValue(QStringLiteral("strands/twists"), s.twists);
    m_store.setValue(QStringLiteral("strands/spinRate"), s.spinRate);
    m_store.setValue(QStringLiteral("strands/opacity"), s.opacity);
    m_store.setValue(QStringLiteral("strands/audioGain"), s.audioGain);
    Q_EMIT visualiserParametersChanged();
}

RenderStyle::Ridge Settings::ridge() const
{
    const RenderStyle defaults;
    RenderStyle::Ridge r = defaults.ridge;
    const auto num = [this](const char *key, qreal fallback) {
        return m_store.value(QLatin1String(key), fallback).toDouble();
    };
    r.rows = m_store.value(QStringLiteral("ridge/rows"), r.rows).toInt();
    r.subdivision = m_store.value(QStringLiteral("ridge/subdivision"), r.subdivision).toInt();
    r.columnStride = m_store.value(QStringLiteral("ridge/columnStride"), r.columnStride).toInt();
    r.depth = num("ridge/depth", r.depth);
    r.amplitude = num("ridge/amplitude", r.amplitude);
    r.lineWidthPt = num("ridge/lineWidthPt", r.lineWidthPt);
    return r;
}

void Settings::setRidge(const RenderStyle::Ridge &r)
{
    m_store.setValue(QStringLiteral("ridge/rows"), r.rows);
    m_store.setValue(QStringLiteral("ridge/subdivision"), r.subdivision);
    m_store.setValue(QStringLiteral("ridge/columnStride"), r.columnStride);
    m_store.setValue(QStringLiteral("ridge/depth"), r.depth);
    m_store.setValue(QStringLiteral("ridge/amplitude"), r.amplitude);
    m_store.setValue(QStringLiteral("ridge/lineWidthPt"), r.lineWidthPt);
    Q_EMIT visualiserParametersChanged();
}

// Clears every stored visualiser parameter, so the next read falls through to
// the defaults rather than to whatever was last saved.
void Settings::resetVisualiserParameters()
{
    m_store.remove(QStringLiteral("strands"));
    m_store.remove(QStringLiteral("ridge"));
    Q_EMIT visualiserParametersChanged();
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
