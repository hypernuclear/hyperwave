#pragma once

#include <memory>

#include <QImage>

#include "render/RenderStyle.h"

namespace hyperwave {

// Bloom and lens dispersion, applied to a finished frame.
//
// It takes the image a visualiser produced and gives it back glowing, which is
// why it is a separate thing from any of them: bars, terrain, strands and lines
// are four different ways of deciding which pixels are lit, and none of them
// has an opinion about what light does afterwards. Written once here, every
// look gets it, including looks that do not exist yet.
//
// Four passes: keep what is bright enough to glow, blur that horizontally, blur
// it vertically, then composite it back over the original with the red and blue
// channels pulled apart along the radius. The last part is what makes the
// reference image look like a photograph of lights rather than a drawing of
// them -- centre lines stay sharp and white, edge lines split and flare.
//
// On the GPU because it is four full-screen passes a frame, which is the
// cheapest thing in the world for a GPU and would double the cost of a CPU
// renderer on its own. The cost is one upload and one readback per frame at
// status item size, which measured at a fraction of a millisecond.
class GpuPost
{
public:
    GpuPost();
    ~GpuPost();

    GpuPost(const GpuPost &) = delete;
    GpuPost &operator=(const GpuPost &) = delete;

    // Applies bloom to `image` in place. Does nothing, quietly, if there is no
    // GPU or a pipeline fails to build: a visualiser that loses its glow is a
    // great deal better than one that stops drawing.
    void apply(QImage &image, const RenderStyle::Bloom &bloom);

    static bool isAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperwave
