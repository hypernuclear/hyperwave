#include "render/Palette.h"

#include <algorithm>

namespace hyperwave {

namespace {

const QVector<GradientPreset> &table()
{
    static const QVector<GradientPreset> kPresets = {
        // The signature. Violet at the baseline into cyan at full height, as
        // one ramp across the whole strip rather than per bar -- see the
        // gradient comment in BarRenderer for why that distinction matters.
        // The peak colour is a tinted, translucent version of the gradient's
        // top rather than white. Pure white dots technically read best, which
        // is the problem: they read as a second, separate object floating above
        // the bars instead of as part of the same instrument.
        {QStringLiteral("signature"), QStringLiteral("Signature"),
         QColor(0x22, 0xD3, 0xEE), QColor(0x7C, 0x3A, 0xED),
         QColor(0x9C, 0xE8, 0xFF, 205), false},

        {QStringLiteral("ember"), QStringLiteral("Ember"),
         QColor(0xFF, 0xD1, 0x66), QColor(0xE0, 0x3E, 0x3E),
         QColor(0xFF, 0xE8, 0xB4, 205), false},

        {QStringLiteral("lagoon"), QStringLiteral("Lagoon"),
         QColor(0x55, 0xD6, 0xBE), QColor(0x08, 0x67, 0x88),
         QColor(0xC6, 0xFF, 0xF2, 205), false},

        // Not a pair of colours at all: see RenderStyle::rainbow. The two
        // gradient fields still carry the ramp's endpoints so that anything
        // reading only those -- an unconverted renderer, the settings preview --
        // gets red-to-violet rather than nothing.
        {QStringLiteral("rainbow"), QStringLiteral("Rainbow"),
         QColor(0xB4, 0x3A, 0xE8), QColor(0xFF, 0x3B, 0x30),
         QColor(0xFF, 0xFF, 0xFF, 205), false, true},

        // Takes the menu bar's own foreground colour, so it adapts to light and
        // dark and never fights the wallpaper. The safe choice, kept available.
        {QStringLiteral("mono"), QStringLiteral("Adaptive mono"),
         QColor(), QColor(), QColor(), true},
    };
    return kPresets;
}

} // namespace

const QVector<GradientPreset> &Palette::presets()
{
    return table();
}

const GradientPreset &Palette::byId(const QString &id)
{
    const auto &all = table();
    for (const auto &p : all) {
        if (p.id == id)
            return p;
    }
    return all.first();
}

QString Palette::defaultId()
{
    return QStringLiteral("signature");
}

void Palette::applyTo(RenderStyle &style, const QString &presetId, const QColor &foreground)
{
    const GradientPreset &p = byId(presetId);

    if (p.followsSystem) {
        QColor top = foreground;
        QColor bottom = foreground;
        // A flat monochrome row is dead. Fading the base of the ramp keeps the
        // same height-encodes-colour reading the coloured presets get.
        bottom.setAlphaF(0.55f);
        style.gradientTop = top;
        style.gradientBottom = bottom;
        style.peakColor = foreground;
    } else {
        style.gradientTop = p.top;
        style.gradientBottom = p.bottom;
        style.peakColor = p.peak;
    }

    style.rainbow = p.isRainbow;
    style.shadowColor = QColor(0, 0, 0, 42);
}

QColor Palette::rainbowAt(qreal t)
{
    // Stops at violet rather than wrapping back to red. A hue ramp that closes
    // the loop has no visible start or end, which is exactly the information
    // these looks are using it to carry: which end is near, which is far.
    constexpr qreal kSpan = 300.0 / 360.0;
    // Held below full saturation for the same reason the signature preset is
    // not pure spectral colour: a fully saturated hue wheel reads as a test
    // pattern, and the blues go nearly black against a dark bar.
    return QColor::fromHsvF(std::clamp(t, 0.0, 1.0) * kSpan, 0.85, 1.0);
}

void Palette::addRainbowStops(QGradient &gradient)
{
    // Seven, which is enough that the interpolation between them is invisible
    // and few enough to stay cheap to rebuild.
    constexpr int kStops = 7;
    for (int i = 0; i < kStops; ++i) {
        const qreal t = static_cast<qreal>(i) / (kStops - 1);
        gradient.setColorAt(t, rainbowAt(t));
    }
}

} // namespace hyperwave
