#pragma once

#include <vector>

#include "render/Visualiser.h"

namespace hyperwave {

// Thin strokes mirrored about the centre line, like a waveform.
//
// The bars grow from the bottom because that is what a level meter does. This
// grows from the middle in both directions, which is what a waveform does, and
// the difference is not decorative: a symmetric shape has no baseline, so the
// eye reads the envelope rather than the tops.
//
// Movement runs through a spring rather than the shared ballistics. A bar that
// eases to its target arrives and stops; one that overshoots slightly and
// settles back has weight, and at these widths that small bounce is most of the
// character.
class MirrorRenderer : public Visualiser
{
public:
    void render(const FrameState &state, const RenderStyle &style, qreal heightPt, qreal dpr,
                QImage &target) override;
    void reset() override;

private:
    // Position and velocity per stroke. A spring needs both; an eased value
    // needs only the first, which is exactly why an eased value cannot bounce.
    std::vector<float> m_value;
    std::vector<float> m_velocity;
};

} // namespace hyperwave
