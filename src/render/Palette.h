#pragma once

#include <QString>
#include <QVector>

#include "render/RenderStyle.h"

namespace hyperlight {

// The named looks, as data rather than as code paths.
//
// The signature gradient is the default; the alternates exist so that adding
// "adaptive monochrome" or "spectrum-mapped hue" is an entry in a table rather
// than a branch in the renderer.
struct GradientPreset
{
    QString id;
    QString name;
    QColor top;
    QColor bottom;
    QColor peak;
    // Monochrome presets take their colour from the host instead of from the
    // table, so they can follow a light or dark menu bar.
    bool followsSystem = false;
};

class Palette
{
public:
    static const QVector<GradientPreset> &presets();
    static const GradientPreset &byId(const QString &id);
    static QString defaultId();

    // Fills the colour fields of `style` from a preset. `foreground` is only
    // consulted by presets that follow the system.
    static void applyTo(RenderStyle &style, const QString &presetId, const QColor &foreground);
};

} // namespace hyperlight
