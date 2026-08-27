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

RenderStyle::Lines Settings::lines() const
{
    const RenderStyle defaults;
    RenderStyle::Lines l = defaults.lines;
    const auto num = [this](const char *key, qreal fallback) {
        return m_store.value(QLatin1String(key), fallback).toDouble();
    };
    l.count = m_store.value(QStringLiteral("lines/count"), l.count).toInt();
    l.widthPt = num("lines/widthPt", l.widthPt);
    l.verticalInset = num("lines/verticalInset", l.verticalInset);
    l.heightResponse = num("lines/heightResponse", l.heightResponse);
    l.floorLevel = num("lines/floorLevel", l.floorLevel);
    l.gamma = num("lines/gamma", l.gamma);
    l.attack = num("lines/attack", l.attack);
    l.release = num("lines/release", l.release);
    return l;
}
void Settings::setLines(const RenderStyle::Lines &l)
{
    m_store.setValue(QStringLiteral("lines/count"), l.count);
    m_store.setValue(QStringLiteral("lines/widthPt"), l.widthPt);
    m_store.setValue(QStringLiteral("lines/verticalInset"), l.verticalInset);
    m_store.setValue(QStringLiteral("lines/heightResponse"), l.heightResponse);
    m_store.setValue(QStringLiteral("lines/floorLevel"), l.floorLevel);
    m_store.setValue(QStringLiteral("lines/gamma"), l.gamma);
    m_store.setValue(QStringLiteral("lines/attack"), l.attack);
    m_store.setValue(QStringLiteral("lines/release"), l.release);
    Q_EMIT visualiserParametersChanged();
}
RenderStyle::Mirror Settings::mirror() const
{
    const RenderStyle defaults;
    RenderStyle::Mirror m = defaults.mirror;
    const auto num = [this](const char *key, qreal fallback) {
        return m_store.value(QLatin1String(key), fallback).toDouble();
    };
    m.count = m_store.value(QStringLiteral("mirror/count"), m.count).toInt();
    m.widthPt = num("mirror/widthPt", m.widthPt);
    m.verticalInset = num("mirror/verticalInset", m.verticalInset);
    m.stiffness = num("mirror/stiffness", m.stiffness);
    m.damping = num("mirror/damping", m.damping);
    return m;
}
void Settings::setMirror(const RenderStyle::Mirror &m)
{
    m_store.setValue(QStringLiteral("mirror/count"), m.count);
    m_store.setValue(QStringLiteral("mirror/widthPt"), m.widthPt);
    m_store.setValue(QStringLiteral("mirror/verticalInset"), m.verticalInset);
    m_store.setValue(QStringLiteral("mirror/stiffness"), m.stiffness);
    m_store.setValue(QStringLiteral("mirror/damping"), m.damping);
    Q_EMIT visualiserParametersChanged();
}
RenderStyle::Ring Settings::ring() const
{
    const RenderStyle defaults;
    RenderStyle::Ring r = defaults.ring;
    const auto num = [this](const char *key, qreal fallback) {
        return m_store.value(QLatin1String(key), fallback).toDouble();
    };
    r.segments = m_store.value(QStringLiteral("ring/segments"), r.segments).toInt();
    r.baseRadius = num("ring/baseRadius", r.baseRadius);
    r.reach = num("ring/reach", r.reach);
    r.lineWidthPt = num("ring/lineWidthPt", r.lineWidthPt);
    r.separation = num("ring/separation", r.separation);
    r.stiffness = num("ring/stiffness", r.stiffness);
    r.damping = num("ring/damping", r.damping);
    return r;
}
void Settings::setRing(const RenderStyle::Ring &r)
{
    m_store.setValue(QStringLiteral("ring/segments"), r.segments);
    m_store.setValue(QStringLiteral("ring/baseRadius"), r.baseRadius);
    m_store.setValue(QStringLiteral("ring/reach"), r.reach);
    m_store.setValue(QStringLiteral("ring/lineWidthPt"), r.lineWidthPt);
    m_store.setValue(QStringLiteral("ring/separation"), r.separation);
    m_store.setValue(QStringLiteral("ring/stiffness"), r.stiffness);
    m_store.setValue(QStringLiteral("ring/damping"), r.damping);
    Q_EMIT visualiserParametersChanged();
}
RenderStyle::Bloom Settings::bloom() const
{
    const RenderStyle defaults;
    RenderStyle::Bloom b = defaults.bloom;
    const auto num = [this](const char *key, qreal fallback) {
        return m_store.value(QLatin1String(key), fallback).toDouble();
    };
    b.enabled = m_store.value(QStringLiteral("bloom/enabled"), b.enabled).toBool();
    b.threshold = num("bloom/threshold", b.threshold);
    b.knee = num("bloom/knee", b.knee);
    b.intensity = num("bloom/intensity", b.intensity);
    b.radius = num("bloom/radius", b.radius);
    b.dispersion = num("bloom/dispersion", b.dispersion);
    b.radialBlur = num("bloom/radialBlur", b.radialBlur);
    return b;
}
void Settings::setBloom(const RenderStyle::Bloom &b)
{
    m_store.setValue(QStringLiteral("bloom/enabled"), b.enabled);
    m_store.setValue(QStringLiteral("bloom/threshold"), b.threshold);
    m_store.setValue(QStringLiteral("bloom/knee"), b.knee);
    m_store.setValue(QStringLiteral("bloom/intensity"), b.intensity);
    m_store.setValue(QStringLiteral("bloom/radius"), b.radius);
    m_store.setValue(QStringLiteral("bloom/dispersion"), b.dispersion);
    m_store.setValue(QStringLiteral("bloom/radialBlur"), b.radialBlur);
    Q_EMIT visualiserParametersChanged();
}
// Clears every stored visualiser parameter, so the next read falls through to
// the defaults rather than to whatever was last saved.
void Settings::resetVisualiserParameters()
{
    m_store.remove(QStringLiteral("strands"));
    m_store.remove(QStringLiteral("ridge"));
    m_store.remove(QStringLiteral("lines"));
    m_store.remove(QStringLiteral("mirror"));
    m_store.remove(QStringLiteral("ring"));
    m_store.remove(QStringLiteral("bloom"));
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
