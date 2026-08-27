#pragma once

#include <vector>

#include "render/Visualiser.h"

namespace hyperwave {

// A closed ring pushed outwards by the spectrum, drawn several times in
// separated colours.
//
// Two things make the reference version of this work. The outline is mirrored,
// so the spectrum runs up one side and back down the other and the shape stays
// symmetric however the music moves -- an asymmetric blob reads as a mistake
// where a symmetric one reads as designed. And the coloured copies are offset
// from the protrusions rather than from the centre: each copy is drawn at a
// slightly different amplitude, so they coincide where the ring is at rest and
// separate into fringes exactly where it bulges.
//
// The glow around it is not done here. That is the shared bloom pass, which is
// also what turns the separated copies from three flat outlines into something
// that looks like light through a lens.
class RingRenderer : public Visualiser
{
public:
    QSize pixelSize(const RenderStyle &style, qreal heightPt, qreal dpr) const override;
    void render(const FrameState &state, const RenderStyle &style, qreal heightPt, qreal dpr,
                QImage &target) override;
    void reset() override;

private:
    std::vector<float> m_value;
    std::vector<float> m_velocity;
    std::vector<QPointF> m_points;
};

} // namespace hyperwave
