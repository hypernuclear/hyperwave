#include "render/GpuContext.h"

#include <rhi/qrhi.h>

namespace hyperwave::GpuContext {

namespace {
std::weak_ptr<QRhi> g_rhi;
}

std::shared_ptr<QRhi> acquire()
{
    if (auto existing = g_rhi.lock())
        return existing;

#if defined(Q_OS_MACOS)
    QRhiMetalInitParams params;
    auto rhi = std::shared_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
#elif defined(Q_OS_WIN)
    QRhiD3D11InitParams params;
    auto rhi = std::shared_ptr<QRhi>(QRhi::create(QRhi::D3D11, &params));
#else
    QRhiVulkanInitParams params;
    auto rhi = std::shared_ptr<QRhi>(QRhi::create(QRhi::Vulkan, &params));
#endif
    g_rhi = rhi;
    return rhi;
}

bool isAvailable()
{
    static const bool available = acquire() != nullptr;
    return available;
}

} // namespace hyperwave::GpuContext
