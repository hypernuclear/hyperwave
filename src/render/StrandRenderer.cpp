#include "render/StrandRenderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>

#include <rhi/qrhi.h>

#include "dsp/DspConfig.h"
#include "render/GpuContext.h"
#include "render/RenderStyle.h"

namespace hyperwave {

namespace {

// The spectrum is uploaded as a 1D texture the vertex shader samples along the
// strand. 256 wide because that is what the original used and it is far more
// resolution than 28 bands need -- the extra is smooth interpolation between
// bands, which is exactly what stops the tube looking like a bar chart.
constexpr int kAudioTextureWidth = 256;

QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(f.readAll());
}

} // namespace

struct StrandRenderer::Impl
{
    std::shared_ptr<QRhi> rhi;

    std::unique_ptr<QRhiTexture> colour;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;

    std::unique_ptr<QRhiBuffer> vertexBuffer;
    std::unique_ptr<QRhiBuffer> uniformBuffer;
    std::unique_ptr<QRhiTexture> audioTexture;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;

    QSize size;
    int segments = 0;
    int vertexCount = 0;
    bool geometryUploaded = false;
    bool failed = false;

    QElapsedTimer clock;
    QImage staging;

    void releaseSizeDependent()
    {
        pipeline.reset();
        bindings.reset();
        target.reset();
        pass.reset();
        depth.reset();
        colour.reset();
    }

    void releaseAll()
    {
        releaseSizeDependent();
        sampler.reset();
        audioTexture.reset();
        uniformBuffer.reset();
        vertexBuffer.reset();
        geometryUploaded = false;
        segments = 0;
        size = {};
    }

    // The strand, as a triangle strip: two vertices per segment, offset to
    // either side of the line by the vertex shader. Positions only differ in x,
    // so previous and next are just the neighbouring samples clamped at the
    // ends -- they exist to give each segment a screen-space direction.
    bool buildGeometry(int wantSegments)
    {
        if (geometryUploaded && segments == wantSegments)
            return true;

        segments = std::clamp(wantSegments, 2, 4096);
        vertexCount = segments * 2;

        struct Vertex
        {
            float position[3];
            float previous[3];
            float next[3];
            float direction;
        };
        static_assert(sizeof(Vertex) == 10 * sizeof(float));

        std::vector<Vertex> vertices;
        vertices.reserve(static_cast<std::size_t>(vertexCount));
        const auto xAt = [this](int i) {
            const int c = std::clamp(i, 0, segments - 1);
            return -1.0f + 2.0f * static_cast<float>(c) / static_cast<float>(segments - 1);
        };
        for (int i = 0; i < segments; ++i) {
            const float x = xAt(i), p = xAt(i - 1), n = xAt(i + 1);
            for (float dir : {1.0f, -1.0f})
                vertices.push_back({{x, 0.0f, 0.0f}, {p, 0.0f, 0.0f}, {n, 0.0f, 0.0f}, dir});
        }

        const quint32 bytes = static_cast<quint32>(vertices.size() * sizeof(Vertex));
        vertexBuffer.reset(
            rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
        if (!vertexBuffer->create())
            return false;

        pendingGeometry.assign(reinterpret_cast<const char *>(vertices.data()),
                               reinterpret_cast<const char *>(vertices.data()) + bytes);
        geometryUploaded = false;
        return true;
    }

    std::vector<char> pendingGeometry;
};

StrandRenderer::StrandRenderer()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->clock.start();
}

StrandRenderer::~StrandRenderer()
{
    if (m_impl)
        m_impl->releaseAll();
}

bool StrandRenderer::isAvailable()
{
    static const bool available = [] {
        return GpuContext::acquire() != nullptr;
    }();
    return available;
}

void StrandRenderer::reset()
{
    m_impl->clock.restart();
}


void StrandRenderer::render(const FrameState &state, const RenderStyle &style, qreal heightPt,
                            qreal dpr, QImage &target)
{
    Q_UNUSED(heightPt);

    Impl &d = *m_impl;
    if (d.failed || target.isNull())
        return;

    const int w = target.width();
    const int h = target.height();
    if (w <= 0 || h <= 0)
        return;

    if (!d.rhi) {
        d.rhi = GpuContext::acquire();
        if (!d.rhi) {
            d.failed = true;
            return;
        }
    }
    QRhi *rhi = d.rhi.get();

    const RenderStyle::Strands &s = style.strands;
    const int strandCount = std::clamp(s.count, 1, 4096);

    if (!d.buildGeometry(s.segments)) {
        d.failed = true;
        return;
    }

    if (d.size != QSize(w, h)) {
        d.releaseSizeDependent();
        d.size = QSize(w, h);

        d.colour.reset(rhi->newTexture(QRhiTexture::RGBA8, d.size, 1,
                                       QRhiTexture::RenderTarget
                                           | QRhiTexture::UsedAsTransferSource));
        if (!d.colour->create()) {
            d.failed = true;
            return;
        }
        d.target.reset(rhi->newTextureRenderTarget({{d.colour.get()}}));
        d.pass.reset(d.target->newCompatibleRenderPassDescriptor());
        d.target->setRenderPassDescriptor(d.pass.get());
        if (!d.target->create()) {
            d.failed = true;
            return;
        }
    }

    if (!d.uniformBuffer) {
        // mat4 + 2 vec4 + 8 floats, padded to a std140-safe size.
        d.uniformBuffer.reset(
            rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 128));
        if (!d.uniformBuffer->create()) {
            d.failed = true;
            return;
        }
    }
    if (!d.audioTexture) {
        d.audioTexture.reset(
            rhi->newTexture(QRhiTexture::R8, QSize(kAudioTextureWidth, 1), 1));
        if (!d.audioTexture->create()) {
            d.failed = true;
            return;
        }
        d.sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                        QRhiSampler::None, QRhiSampler::ClampToEdge,
                                        QRhiSampler::ClampToEdge));
        if (!d.sampler->create()) {
            d.failed = true;
            return;
        }
    }

    if (!d.pipeline) {
        d.bindings.reset(rhi->newShaderResourceBindings());
        d.bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, d.uniformBuffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::VertexStage, d.audioTexture.get(),
                d.sampler.get()),
        });
        if (!d.bindings->create()) {
            d.failed = true;
            return;
        }

        const QShader vs = loadShader(QStringLiteral(":/shaders/strands.vert.qsb"));
        const QShader fs = loadShader(QStringLiteral(":/shaders/strands.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("hyperwave: strand shaders missing; falling back");
            d.failed = true;
            return;
        }

        d.pipeline.reset(rhi->newGraphicsPipeline());
        d.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        d.pipeline->setCullMode(QRhiGraphicsPipeline::None);
        d.pipeline->setShaderStages({{QRhiShaderStage::Vertex, vs},
                                     {QRhiShaderStage::Fragment, fs}});

        // Premultiplied blending, because the shader emits premultiplied colour
        // and the destination is ARGB32_Premultiplied. Getting this wrong shows
        // up as dark fringes where strands overlap, which reads as a bug in the
        // gradient rather than in the blend state.
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        d.pipeline->setTargetBlends({blend});

        QRhiVertexInputLayout layout;
        layout.setBindings({{10 * sizeof(float)}});
        layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3, 0},
            {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
            {0, 2, QRhiVertexInputAttribute::Float3, 6 * sizeof(float)},
            {0, 3, QRhiVertexInputAttribute::Float, 9 * sizeof(float)},
        });
        d.pipeline->setVertexInputLayout(layout);
        d.pipeline->setShaderResourceBindings(d.bindings.get());
        d.pipeline->setRenderPassDescriptor(d.pass.get());
        if (!d.pipeline->create()) {
            d.failed = true;
            return;
        }
    }

    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return;

    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();

    if (!d.geometryUploaded && !d.pendingGeometry.empty()) {
        u->uploadStaticBuffer(d.vertexBuffer.get(), d.pendingGeometry.data());
        d.geometryUploaded = true;
    }

    // The spectrum, stretched across 256 texels. Interpolating between bands
    // here rather than in the shader keeps the sampling a straight lookup.
    {
        const int bands = std::clamp(state.bandCount, 1, dsp::kMaxBandCount);
        QImage spectrum(kAudioTextureWidth, 1, QImage::Format_Grayscale8);
        uchar *line = spectrum.bits();
        for (int i = 0; i < kAudioTextureWidth; ++i) {
            const float u01 =
                static_cast<float>(i) / static_cast<float>(kAudioTextureWidth - 1);
            const float pos = u01 * static_cast<float>(bands - 1);
            const int i0 = std::clamp(static_cast<int>(pos), 0, bands - 1);
            const int i1 = std::clamp(i0 + 1, 0, bands - 1);
            const float t = pos - static_cast<float>(i0);
            const float v = state.bars[i0] * (1.0f - t) + state.bars[i1] * t;
            line[i] = static_cast<uchar>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }
        u->uploadTexture(d.audioTexture.get(), spectrum);
    }

    {
        const qreal seconds = d.clock.elapsed() / 1000.0;
        const float aspect = static_cast<float>(w) / static_cast<float>(h);

        // Straight to clip space, no ortho.
        //
        // The strand is already unit-length in x, and clip space is [-1,1] in
        // both axes with the viewport doing the aspect correction, so an
        // orthographic projection over [-aspect, aspect] only divides x by the
        // aspect a second time -- which on a 236x44 strip shrank the whole tube
        // to about a fifth of its width and made it look like nothing had
        // rendered at all.
        QMatrix4x4 mvp = rhi->clipSpaceCorrMatrix();
        // Read back top-down into the QImage, so the vertical axis flips once,
        // here, rather than by mirroring 41KB of pixels every frame.
        mvp.scale(1.0f, -1.0f, 1.0f);
        mvp.rotate(static_cast<float>(s.tiltDegrees), 1.0f, 0.0f, 0.0f);
        mvp.scale(0.95f, static_cast<float>(s.fill), static_cast<float>(s.fill));

        // Thickness is authored in points and has to become clip units, where
        // the full height is 2.0.
        const float thickness =
            static_cast<float>(s.thicknessPt * dpr) * 2.0f / static_cast<float>(h);

        const qreal presence = std::clamp<qreal>(state.presence, 0.0, 1.0);
        const qreal opacity = s.opacity * (style.idleOpacity
                                           + (1.0 - style.idleOpacity) * presence);

        float block[32] = {};
        const float *m = mvp.constData();
        for (int i = 0; i < 16; ++i)
            block[i] = m[i];
        block[16] = style.gradientBottom.redF();
        block[17] = style.gradientBottom.greenF();
        block[18] = style.gradientBottom.blueF();
        // colorA's alpha is the rainbow flag. Both colour alphas were dead
        // weight -- written as 1.0, never read -- and the block is exactly full
        // at 32 floats, so this carries the flag without growing it.
        block[19] = style.rainbow ? 1.0f : 0.0f;
        block[20] = style.gradientTop.redF();
        block[21] = style.gradientTop.greenF();
        block[22] = style.gradientTop.blueF();
        // colorB's alpha is the depth half-extent the rainbow normalises by;
        // see the comment in strands.vert. The tube's depth comes entirely from
        // its cross-section, which the projection scales by `fill` and the
        // clip-space correction halves. The turbulence term is what the noise
        // adds to the radius on average -- an estimate, and it only has to be
        // close: too small clips the extremes to flat red and violet, too large
        // leaves the ends of the ramp unused. Measured against the tube at rest
        // this is the knob that decides how much of the ramp a quiet passage
        // shows -- 0.02 spends the whole rainbow across the depth of the tube,
        // and raising it towards 0.1 pulls the quiet stretches back to green
        // with the spectrum only opening up on the loud, deep parts.
        const float rainbowExtent =
            0.5f * static_cast<float>(s.fill) * (static_cast<float>(s.radius) + 0.02f);
        block[23] = std::max(rainbowExtent, 1e-4f);
        block[24] = aspect;
        block[25] = static_cast<float>(seconds * s.spinRate);
        block[26] = static_cast<float>(s.radius);
        block[27] = thickness;
        block[28] = static_cast<float>(s.twists);
        block[29] = static_cast<float>(opacity);
        block[30] = static_cast<float>(strandCount);
        block[31] = static_cast<float>(s.audioGain);
        u->updateDynamicBuffer(d.uniformBuffer.get(), 0, sizeof(block), block);
    }

    cb->beginPass(d.target.get(), QColor(0, 0, 0, 0), {1.0f, 0}, u);
    cb->setGraphicsPipeline(d.pipeline.get());
    cb->setViewport({0, 0, float(w), float(h)});
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vi(d.vertexBuffer.get(), 0);
    cb->setVertexInput(0, 1, &vi);
    cb->draw(static_cast<quint32>(d.vertexCount), static_cast<quint32>(strandCount));
    cb->endPass();

    QRhiReadbackResult rb;
    QRhiResourceUpdateBatch *readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({d.colour.get()}, &rb);
    cb->resourceUpdate(readback);
    rhi->endOffscreenFrame();

    if (rb.data.isEmpty() || rb.pixelSize != QSize(w, h))
        return;

    // RGBA8 out of the GPU, ARGB32 in the QImage: the same four bytes in a
    // different order, so this is a wrapper plus one convert, not a decode.
    const QImage view(reinterpret_cast<const uchar *>(rb.data.constData()), w, h,
                      QImage::Format_RGBA8888_Premultiplied);
    QPainter p(&target);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(0, 0, view);
}

} // namespace hyperwave
