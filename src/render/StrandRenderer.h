#pragma once

#include <memory>

#include <QSize>

#include "render/Visualiser.h"

QT_BEGIN_NAMESPACE
class QRhi;
QT_END_NAMESPACE

namespace hyperwave {

// A bundle of strands wrapped around an axis, on the GPU.
//
// The technique is Matt DesLauriers' codevember #21, ported: one straight line
// along X per strand, wrapped radially at a radius that the spectrum and three
// octaves of simplex noise push around, tapered to points at both ends, drawn
// as expanded ribbons. X is frequency, so the tube swells where the music is.
//
// It is on the GPU because that is what the technique *is*, not because the GPU
// was available. The geometry is static -- uploaded once, never touched -- and
// every frame's motion is a vertex shader over it. Sixty-four strands of two
// hundred segments is 25,600 vertices with three noise evaluations each, which
// is roughly 77,000 noise evaluations per frame. On the CPU that is not a
// tuning problem, it is the wrong machine: the CPU-side ridge renderer costs
// 22% of a core doing far less work than this.
//
// Rendered offscreen and read back, because every presenter here consumes a
// QImage -- the status item hands bytes to Core Animation, the Windows tray
// icon needs a bitmap. Measured on Metal at status item size, the whole round
// trip (submit, wait, copy 41KB back) is 0.166 ms, about 1% of a core at 60fps.
// A frame of latency, which on a visualiser nobody can see.
class StrandRenderer : public Visualiser
{
public:
    StrandRenderer();
    ~StrandRenderer() override;

    void render(const FrameState &state, const RenderStyle &style, qreal heightPt, qreal dpr,
                QImage &target) override;
    void reset() override;

    // Whether a GPU is actually available. Checked once; a machine that cannot
    // create a QRhi has to fall back to a look that does not need one rather
    // than showing an empty menu bar item.
    static bool isAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperwave
