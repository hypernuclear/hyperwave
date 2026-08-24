#include "render/Palette.h"

namespace hyperlight {

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

    style.shadowColor = QColor(0, 0, 0, 42);
}

} // namespace hyperlight
