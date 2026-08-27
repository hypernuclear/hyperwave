#include "render/GpuPost.h"

#include <algorithm>
#include <array>

#include <QFile>

#include <rhi/qrhi.h>

#include "render/GpuContext.h"

namespace hyperwave {

namespace {

QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(f.readAll());
}

// How much the bright pass and the blurs are downscaled by.
//
// Bloom is low-frequency by definition, so detail thrown away here was about to
// be blurred out anyway, and a quarter in each direction makes those three
// passes a sixteenth of the work while multiplying the kernel's reach for free.
//
// But only when there is resolution to spare. The ring asks for a square status
// item, which is 44 pixels across at 2x -- a quarter of that is an eleven-pixel
// bloom texture, and blurring eleven pixels and compositing them back over
// forty-four does not read as glow, it reads as the whole thing being out of
// focus. So the divisor backs off on small targets and the bloom is computed at
// full resolution when the frame is small enough that it costs nothing anyway.
int bloomDivisorFor(const QSize &size)
{
    const int shortSide = std::min(size.width(), size.height());
    return std::clamp(shortSide / 64, 1, 4);
}

struct Uniforms
{
    float texelSize[2] = {0.0f, 0.0f};
    float threshold = 0.0f;
    float knee = 0.0f;
    float intensity = 0.0f;
    float direction = 0.0f;
    float dispersion = 0.0f;
    float radialBlur = 0.0f;
    float rayLength = 0.0f;
    float rayDecay = 0.0f;
    float pad[2] = {0.0f, 0.0f};
};
// std140 rounds a uniform block up to a multiple of sixteen bytes, so the
// padding is not decoration: without it the driver reads two floats of
// whatever follows.
static_assert(sizeof(Uniforms) == 48);

} // namespace

struct GpuPost::Impl
{
    std::shared_ptr<QRhi> rhi;

    QSize size;
    QSize bloomSize;
    bool failed = false;

    std::unique_ptr<QRhiTexture> source;    // what the visualiser drew
    std::unique_ptr<QRhiTexture> bright;    // half res
    std::unique_ptr<QRhiTexture> blurTmp;   // half res
    std::unique_ptr<QRhiTexture> bloom;     // half res
    std::unique_ptr<QRhiTexture> rays;      // half res
    std::unique_ptr<QRhiTexture> output;

    std::unique_ptr<QRhiTextureRenderTarget> brightTarget, blurTmpTarget, bloomTarget,
        raysTarget, outputTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> brightPass, blurPass, bloomPass, raysPass,
        outputPass;

    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiBuffer> uniforms;   // one block, rewritten per pass

    std::unique_ptr<QRhiShaderResourceBindings> brightSrb, blurTmpSrb, bloomSrb, raysSrb,
        outputSrb;
    std::unique_ptr<QRhiGraphicsPipeline> brightPipeline, blurPipeline, raysPipeline,
        compositePipeline;

    void releaseSizeDependent()
    {
        brightPipeline.reset();
        blurPipeline.reset();
        raysPipeline.reset();
        compositePipeline.reset();
        brightSrb.reset();
        blurTmpSrb.reset();
        bloomSrb.reset();
        raysSrb.reset();
        outputSrb.reset();
        brightTarget.reset();
        blurTmpTarget.reset();
        bloomTarget.reset();
        raysTarget.reset();
        outputTarget.reset();
        brightPass.reset();
        blurPass.reset();
        bloomPass.reset();
        raysPass.reset();
        outputPass.reset();
        source.reset();
        bright.reset();
        blurTmp.reset();
        bloom.reset();
        rays.reset();
        output.reset();
        size = {};
    }
};

GpuPost::GpuPost()
    : m_impl(std::make_unique<Impl>())
{
}

GpuPost::~GpuPost()
{
    if (m_impl)
        m_impl->releaseSizeDependent();
}

bool GpuPost::isAvailable()
{
    return GpuContext::isAvailable();
}

void GpuPost::apply(QImage &image, const RenderStyle::Bloom &params)
{
    Impl &d = *m_impl;
    if (!params.enabled || d.failed || image.isNull())
        return;

    const int w = image.width();
    const int h = image.height();
    if (w < 4 || h < 4)
        return;

    if (!d.rhi) {
        d.rhi = GpuContext::acquire();
        if (!d.rhi) {
            d.failed = true;
            return;
        }
    }
    QRhi *rhi = d.rhi.get();

    if (d.size != QSize(w, h)) {
        d.releaseSizeDependent();
        d.size = QSize(w, h);
        const int divisor = bloomDivisorFor(d.size);
        d.bloomSize = QSize(std::max(1, w / divisor), std::max(1, h / divisor));

        const auto makeTexture = [&](std::unique_ptr<QRhiTexture> &tex, QSize size,
                                     QRhiTexture::Flags flags) {
            tex.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1, flags));
            return tex->create();
        };
        const auto makeTarget = [&](std::unique_ptr<QRhiTextureRenderTarget> &rt,
                                    std::unique_ptr<QRhiRenderPassDescriptor> &rp,
                                    QRhiTexture *tex) {
            rt.reset(rhi->newTextureRenderTarget({{tex}}));
            rp.reset(rt->newCompatibleRenderPassDescriptor());
            rt->setRenderPassDescriptor(rp.get());
            return rt->create();
        };

        if (!makeTexture(d.source, d.size, QRhiTexture::Flags{})
            || !makeTexture(d.bright, d.bloomSize, QRhiTexture::RenderTarget)
            || !makeTexture(d.blurTmp, d.bloomSize, QRhiTexture::RenderTarget)
            || !makeTexture(d.bloom, d.bloomSize, QRhiTexture::RenderTarget)
            || !makeTexture(d.rays, d.bloomSize, QRhiTexture::RenderTarget)
            || !makeTexture(d.output, d.size,
                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource)
            || !makeTarget(d.brightTarget, d.brightPass, d.bright.get())
            || !makeTarget(d.blurTmpTarget, d.blurPass, d.blurTmp.get())
            || !makeTarget(d.bloomTarget, d.bloomPass, d.bloom.get())
            || !makeTarget(d.raysTarget, d.raysPass, d.rays.get())
            || !makeTarget(d.outputTarget, d.outputPass, d.output.get())) {
            d.failed = true;
            return;
        }
    }

    if (!d.sampler) {
        // Clamped, so a blur tap that runs off the edge repeats the edge pixel
        // instead of wrapping the glow around to the opposite side.
        d.sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                        QRhiSampler::None, QRhiSampler::ClampToEdge,
                                        QRhiSampler::ClampToEdge));
        if (!d.sampler->create()) {
            d.failed = true;
            return;
        }
    }
    if (!d.uniforms) {
        d.uniforms.reset(
            rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
        if (!d.uniforms->create()) {
            d.failed = true;
            return;
        }
    }

    if (!d.brightPipeline) {
        const QShader vs = loadShader(QStringLiteral(":/shaders/fullscreen.vert.qsb"));
        const QShader brightFs = loadShader(QStringLiteral(":/shaders/bright.frag.qsb"));
        const QShader blurFs = loadShader(QStringLiteral(":/shaders/blur.frag.qsb"));
        const QShader raysFs = loadShader(QStringLiteral(":/shaders/rays.frag.qsb"));
        const QShader compositeFs = loadShader(QStringLiteral(":/shaders/composite.frag.qsb"));
        if (!vs.isValid() || !brightFs.isValid() || !blurFs.isValid() || !raysFs.isValid()
            || !compositeFs.isValid()) {
            qWarning("hyperwave: bloom shaders missing; drawing without it");
            d.failed = true;
            return;
        }

        const auto makeSrb = [&](std::unique_ptr<QRhiShaderResourceBindings> &srb,
                                 QRhiTexture *first, QRhiTexture *second,
                                 QRhiTexture *third = nullptr) {
            QVarLengthArray<QRhiShaderResourceBinding, 4> bindings;
            bindings.append(QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::FragmentStage, d.uniforms.get()));
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, first, d.sampler.get()));
            if (second) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2, QRhiShaderResourceBinding::FragmentStage, second, d.sampler.get()));
            }
            if (third) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    3, QRhiShaderResourceBinding::FragmentStage, third, d.sampler.get()));
            }
            srb.reset(rhi->newShaderResourceBindings());
            srb->setBindings(bindings.cbegin(), bindings.cend());
            return srb->create();
        };

        if (!makeSrb(d.brightSrb, d.source.get(), nullptr)
            || !makeSrb(d.blurTmpSrb, d.bright.get(), nullptr)
            || !makeSrb(d.bloomSrb, d.blurTmp.get(), nullptr)
            || !makeSrb(d.raysSrb, d.bright.get(), nullptr)
            || !makeSrb(d.outputSrb, d.source.get(), d.bloom.get(), d.rays.get())) {
            d.failed = true;
            return;
        }

        const auto makePipeline = [&](std::unique_ptr<QRhiGraphicsPipeline> &ps,
                                      const QShader &fs, QRhiShaderResourceBindings *srb,
                                      QRhiRenderPassDescriptor *rp) {
            ps.reset(rhi->newGraphicsPipeline());
            ps->setTopology(QRhiGraphicsPipeline::Triangles);
            ps->setCullMode(QRhiGraphicsPipeline::None);
            ps->setShaderStages({{QRhiShaderStage::Vertex, vs},
                                 {QRhiShaderStage::Fragment, fs}});
            // No vertex input at all: the vertex shader builds a fullscreen
            // triangle from gl_VertexIndex.
            ps->setVertexInputLayout({});
            ps->setShaderResourceBindings(srb);
            ps->setRenderPassDescriptor(rp);
            return ps->create();
        };

        if (!makePipeline(d.brightPipeline, brightFs, d.brightSrb.get(), d.brightPass.get())
            || !makePipeline(d.blurPipeline, blurFs, d.blurTmpSrb.get(), d.blurPass.get())
            || !makePipeline(d.raysPipeline, raysFs, d.raysSrb.get(), d.raysPass.get())
            || !makePipeline(d.compositePipeline, compositeFs, d.outputSrb.get(),
                             d.outputPass.get())) {
            d.failed = true;
            return;
        }
    }

    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return;

    QRhiResourceUpdateBatch *upload = rhi->nextResourceUpdateBatch();
    // RGBA8888 to match the shader's channel order. The frame arrives as
    // ARGB32_Premultiplied, which is the same four bytes the other way round.
    upload->uploadTexture(d.source.get(),
                          image.convertToFormat(QImage::Format_RGBA8888_Premultiplied));

    Uniforms u;
    u.threshold = static_cast<float>(params.threshold);
    u.knee = static_cast<float>(params.knee);
    u.intensity = static_cast<float>(params.intensity);
    u.dispersion = static_cast<float>(params.dispersion);
    u.radialBlur = static_cast<float>(params.radialBlur);
    u.rayLength = static_cast<float>(params.rayLength);
    u.rayDecay = static_cast<float>(params.rayDecay);

    const auto runPass = [&](QRhiTextureRenderTarget *rt, QRhiGraphicsPipeline *ps,
                             QRhiShaderResourceBindings *srb, const QSize &viewport,
                             const Uniforms &block, QRhiResourceUpdateBatch *extra) {
        QRhiResourceUpdateBatch *batch = extra ? extra : rhi->nextResourceUpdateBatch();
        batch->updateDynamicBuffer(d.uniforms.get(), 0, sizeof(Uniforms), &block);
        cb->beginPass(rt, QColor(0, 0, 0, 0), {1.0f, 0}, batch);
        cb->setGraphicsPipeline(ps);
        cb->setViewport({0, 0, float(viewport.width()), float(viewport.height())});
        cb->setShaderResources(srb);
        cb->draw(3);
        cb->endPass();
    };

    // 1. What is bright enough to glow, at half resolution.
    u.texelSize[0] = 1.0f / float(w);
    u.texelSize[1] = 1.0f / float(h);
    runPass(d.brightTarget.get(), d.brightPipeline.get(), d.brightSrb.get(), d.bloomSize, u,
            upload);

    // 2 and 3. Separable blur, horizontal then vertical.
    const float radius = static_cast<float>(std::max<qreal>(params.radius, 0.1));
    u.texelSize[0] = radius / float(d.bloomSize.width());
    u.texelSize[1] = radius / float(d.bloomSize.height());
    u.direction = 0.0f;
    runPass(d.blurTmpTarget.get(), d.blurPipeline.get(), d.blurTmpSrb.get(), d.bloomSize, u,
            nullptr);
    u.direction = 1.0f;
    runPass(d.bloomTarget.get(), d.blurPipeline.get(), d.bloomSrb.get(), d.bloomSize, u,
            nullptr);

    // 4. Rays, marched from the bright pass towards the centre.
    runPass(d.raysTarget.get(), d.raysPipeline.get(), d.raysSrb.get(), d.bloomSize, u, nullptr);

    // 5. Back over the original, with the channels pulled apart radially.
    u.texelSize[0] = 1.0f / float(w);
    u.texelSize[1] = 1.0f / float(h);
    runPass(d.outputTarget.get(), d.compositePipeline.get(), d.outputSrb.get(), d.size, u,
            nullptr);

    QRhiReadbackResult rb;
    QRhiResourceUpdateBatch *readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({d.output.get()}, &rb);
    cb->resourceUpdate(readback);
    rhi->endOffscreenFrame();

    if (rb.data.isEmpty() || rb.pixelSize != QSize(w, h))
        return;

    const QImage view(reinterpret_cast<const uchar *>(rb.data.constData()), w, h,
                      QImage::Format_RGBA8888_Premultiplied);
    image = view.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace hyperwave
