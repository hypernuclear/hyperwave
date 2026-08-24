#include "audio/AudioSource.h"

// Windows capture: NOT IMPLEMENTED.
//
// The approach is settled even though the code is not. WASAPI loopback is the
// direct analogue of the macOS process tap and it is markedly simpler: activate
// the default render endpoint with IAudioClient, initialise it with
// AUDCLNT_STREAMFLAGS_LOOPBACK, and read capture packets off
// IAudioCaptureClient. No permission prompt, no virtual device, no aggregate.
//
// What to reuse rather than rewrite:
//   * downmixInterleaved / sanitize from audio/Downmix.h. WASAPI hands back
//     interleaved float32 in the mix format, so the existing path applies
//     verbatim.
//   * MonoRing, unchanged. The capture thread is the producer.
//   * formatEpoch, bumped on device change. Watch for that with
//     IMMNotificationClient::OnDefaultDeviceChanged, and debounce it the same
//     250 ms the macOS side does -- device notification storms are not a
//     macOS-specific phenomenon.
//
// One thing that has no macOS equivalent and will be missed: a loopback stream
// delivers NOTHING at all while the endpoint is idle, rather than delivering
// silence. The DSP will simply see no samples, so the silence detection has to
// key off "no packets for N ms" as well as off RMS.
//
// See docs/windows-backend.md.

namespace hyperlight {

namespace {

class WasapiSource : public AudioSource
{
public:
    explicit WasapiSource(QObject *parent) : AudioSource(parent) {}

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
        return QStringLiteral("WASAPI loopback capture is not implemented yet");
    }

private:
    MonoRing m_ring;
};

} // namespace

std::unique_ptr<AudioSource> AudioSource::create(QObject *parent)
{
    return std::make_unique<WasapiSource>(parent);
}

} // namespace hyperlight
