#include "audio/AudioSource.h"

// Linux capture: NOT IMPLEMENTED.
//
// The plan is libpulse's simple API against the default sink's monitor source
// (`<sink>.monitor`), which works on PulseAudio and, through the pulse shim, on
// PipeWire as well -- covering essentially every desktop with a fraction of the
// code a native PipeWire client would take. Native libpipewire is the upgrade
// path if the shim ever proves lacking, not the starting point.
//
// Reuse downmixInterleaved, sanitize and MonoRing exactly as the other backends
// do. Request PA_SAMPLE_FLOAT32NE so no conversion is needed.
//
// The sharp edge: the default sink can change under you and monitor sources
// come and go with it. Subscribe to PA_SUBSCRIPTION_MASK_SERVER, reconnect, and
// bump formatEpoch -- the same shape as the macOS device-change path.
//
// See docs/windows-backend.md, which covers the Linux notes too.

namespace hyperwave {

namespace {

class PulseSource : public AudioSource
{
public:
    explicit PulseSource(QObject *parent) : AudioSource(parent) {}

    bool start() override { return false; }
    void stop() override {}
    bool isRunning() const override { return false; }

    double sampleRate() const override { return 0.0; }
    std::uint32_t formatEpoch() const override { return 0; }
    PermissionState permissionState() const override { return PermissionState::Unsupported; }

    MonoRing *ring() override { return &m_ring; }

    std::uint64_t callbackCount() const override { return 0; }
    std::uint64_t overrunCount() const override { return 0; }
    QString statusDetail() const override
    {
        return QStringLiteral("PulseAudio monitor capture is not implemented yet");
    }

private:
    MonoRing m_ring;
};

} // namespace

std::unique_ptr<AudioSource> AudioSource::create(QObject *parent)
{
    return std::make_unique<PulseSource>(parent);
}

} // namespace hyperwave
