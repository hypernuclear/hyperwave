#include "audio/AudioSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

// initguid.h MUST come before the headers that declare the keys and class ids.
// Without it PKEY_Device_FriendlyName is an extern declaration that nothing
// ever defines, and the failure is a link error rather than a compile one --
// which is exactly the kind of thing that only shows up on the machine that
// actually has the SDK.
#include <initguid.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

// WAVE_FORMAT_EXTENSIBLE and friends come from mmreg.h; the
// KSDATAFORMAT_SUBTYPE_* GUIDs the extensible format is compared against come
// from ksmedia.h. Neither arrives with audioclient.h.
#include <ksmedia.h>
#include <mmreg.h>

#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QDebug>
#include <QString>

#include "audio/Downmix.h"

// Windows capture, via WASAPI loopback.
//
// The direct analogue of the macOS process tap, and markedly simpler: activate
// the render endpoint with IAudioClient, initialise it with
// AUDCLNT_STREAMFLAGS_LOOPBACK, read packets off IAudioCaptureClient. No
// permission prompt, no virtual device, no aggregate device to assemble.
//
// Everything above the ring is shared with macOS -- downmixInterleaved,
// sanitize, MonoRing, the epoch protocol -- so this file's whole job is turning
// an endpoint into mono float and pushing it into a ring at the right rate.
//
// See docs/windows-backend.md.

namespace hyperwave {

namespace {

// Minimal COM pointer. <wrl.h> would do this, but pulling in the Windows
// Runtime Library for one smart pointer is a large dependency for a small job.
template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T **put()
    {
        reset();
        return &m_p;
    }
    void **putVoid() { return reinterpret_cast<void **>(put()); }

    T *get() const { return m_p; }
    T *operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }

    void reset()
    {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }

private:
    T *m_p = nullptr;
};

struct CoTaskMemDeleter
{
    void operator()(void *p) const noexcept
    {
        if (p)
            CoTaskMemFree(p);
    }
};

QString hresultText(HRESULT hr)
{
    // The handful that actually come up, named. Anything else gets its hex,
    // which is what you would search for anyway.
    switch (hr) {
    case AUDCLNT_E_DEVICE_INVALIDATED:
        return QStringLiteral("device invalidated (unplugged or reconfigured)");
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
        return QStringLiteral("the Windows Audio service is not running");
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return QStringLiteral("endpoint format not supported");
    case AUDCLNT_E_DEVICE_IN_USE:
        return QStringLiteral("device is in exclusive-mode use by another app");
    case E_ACCESSDENIED:
        return QStringLiteral("access denied (microphone privacy setting)");
    default:
        break;
    }
    return QStringLiteral("HRESULT 0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// Shared mode almost always hands back float32, but "almost always" is not a
// contract: an unusual endpoint driver can offer 16-bit PCM, and reading that
// as float produces noise loud enough to be alarming rather than silence. Both
// are handled; anything else is refused outright rather than misread.
enum class SampleKind { Unsupported, Float32, Int16 };

SampleKind classify(const WAVEFORMATEX *wf)
{
    if (!wf)
        return SampleKind::Unsupported;

    WORD tag = wf->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (wf->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return SampleKind::Unsupported;
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wf);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
            tag = WAVE_FORMAT_PCM;
        else
            return SampleKind::Unsupported;
    }

    if (tag == WAVE_FORMAT_IEEE_FLOAT && wf->wBitsPerSample == 32)
        return SampleKind::Float32;
    if (tag == WAVE_FORMAT_PCM && wf->wBitsPerSample == 16)
        return SampleKind::Int16;
    return SampleKind::Unsupported;
}

// One endpoint, captured on its own thread, into one ring.
//
// Used twice: in loopback mode on a render endpoint (what the machine is
// playing) and in ordinary capture mode on a capture endpoint (what the
// microphone hears). The two differ by a single flag, so they share everything
// else rather than existing as two near-identical loops.
class CaptureStream
{
public:
    struct Counters
    {
        std::atomic<std::uint64_t> callbacks{0};
        std::atomic<std::uint64_t> overruns{0};
        std::atomic<double> sampleRate{0.0};
        std::atomic<bool> healthy{false};
    };

    ~CaptureStream() { stop(); }

    // `deviceId` empty means "follow the default endpoint".
    void start(bool loopback, QString deviceId, MonoRing *ring, Counters *counters,
               std::atomic<std::uint32_t> *epoch, std::function<void(QString)> onFailure)
    {
        stop();
        m_quit.store(false, std::memory_order_relaxed);
        m_loopback = loopback;
        m_deviceId = std::move(deviceId);
        m_ring = ring;
        m_counters = counters;
        m_epoch = epoch;
        m_onFailure = std::move(onFailure);
        m_thread = std::thread([this] { run(); });
    }

    void stop()
    {
        if (!m_thread.joinable())
            return;
        m_quit.store(true, std::memory_order_relaxed);
        m_thread.join();
        if (m_counters)
            m_counters->healthy.store(false, std::memory_order_relaxed);
    }

private:
    void fail(const QString &what)
    {
        m_counters->healthy.store(false, std::memory_order_relaxed);
        if (m_onFailure)
            m_onFailure(what);
    }

    // Resolve the endpoint this stream should read, and report its id so the
    // loop can notice the default moving out from under it.
    bool openEndpoint(IMMDeviceEnumerator *enumerator, ComPtr<IMMDevice> &device,
                      QString &resolvedId)
    {
        const EDataFlow flow = m_loopback ? eRender : eCapture;
        HRESULT hr;
        if (m_deviceId.isEmpty()) {
            hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.put());
        } else {
            hr = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(m_deviceId.utf16()),
                                       device.put());
            if (FAILED(hr)) {
                // A remembered device that is no longer present is not worth
                // stopping for: fall back to the default and keep running, the
                // way the macOS side falls back to the global tap.
                hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.put());
            }
        }
        if (FAILED(hr) || !device) {
            fail(QStringLiteral("no endpoint: %1").arg(hresultText(hr)));
            return false;
        }

        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)) && id) {
            resolvedId = QString::fromWCharArray(id);
            CoTaskMemFree(id);
        }
        return true;
    }

    QString currentDefaultId(IMMDeviceEnumerator *enumerator) const
    {
        ComPtr<IMMDevice> device;
        const EDataFlow flow = m_loopback ? eRender : eCapture;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.put())) || !device)
            return {};
        LPWSTR id = nullptr;
        QString out;
        if (SUCCEEDED(device->GetId(&id)) && id) {
            out = QString::fromWCharArray(id);
            CoTaskMemFree(id);
        }
        return out;
    }

    void run()
    {
        // MTA: this thread owns its COM objects and never pumps a message loop.
        const bool weInitialised = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

        // Not a realtime thread -- missing a deadline costs a gap in the ring,
        // not audible glitching -- but it must not be starved by a busy UI.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        while (!m_quit.load(std::memory_order_relaxed)) {
            if (!runOnce()) {
                // A failed open is usually transient: the audio service
                // restarting, a device mid-reconfiguration. Back off in short
                // steps rather than spinning, and stay responsive to stop().
                for (int i = 0; i < 20 && !m_quit.load(std::memory_order_relaxed); ++i)
                    Sleep(50);
            }
        }

        if (weInitialised)
            CoUninitialize();
    }

    // One open-capture-close cycle. False if the endpoint could not be opened;
    // true when it exited normally, which includes exiting to rebuild.
    bool runOnce()
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
        if (FAILED(hr) || !enumerator) {
            fail(QStringLiteral("no device enumerator: %1").arg(hresultText(hr)));
            return false;
        }

        ComPtr<IMMDevice> device;
        QString openedId;
        if (!openEndpoint(enumerator.get(), device, openedId))
            return false;

        ComPtr<IAudioClient> client;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.putVoid());
        if (FAILED(hr) || !client) {
            fail(QStringLiteral("cannot activate audio client: %1").arg(hresultText(hr)));
            return false;
        }

        WAVEFORMATEX *rawFormat = nullptr;
        hr = client->GetMixFormat(&rawFormat);
        if (FAILED(hr) || !rawFormat) {
            fail(QStringLiteral("no mix format: %1").arg(hresultText(hr)));
            return false;
        }
        std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> format(rawFormat);

        const SampleKind kind = classify(format.get());
        if (kind == SampleKind::Unsupported) {
            fail(QStringLiteral("unsupported mix format: %1-bit, tag %2")
                     .arg(format->wBitsPerSample)
                     .arg(format->wFormatTag));
            return false;
        }

        const auto channels = static_cast<std::uint32_t>(format->nChannels);
        const auto rate = static_cast<double>(format->nSamplesPerSec);
        if (channels == 0 || rate <= 0.0) {
            fail(QStringLiteral("endpoint reports %1 channels at %2 Hz").arg(channels).arg(rate));
            return false;
        }

        // 200 ms of endpoint buffer. Shared mode treats this as a hint and the
        // poll below is what actually paces us; it only has to be comfortably
        // longer than one poll interval.
        constexpr REFERENCE_TIME kBufferDuration = 2000000; // 100 ns units
        const DWORD flags = m_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0,
                                format.get(), nullptr);
        if (FAILED(hr)) {
            fail(QStringLiteral("cannot initialise capture: %1").arg(hresultText(hr)));
            return false;
        }

        ComPtr<IAudioCaptureClient> capture;
        hr = client->GetService(__uuidof(IAudioCaptureClient), capture.putVoid());
        if (FAILED(hr) || !capture) {
            fail(QStringLiteral("no capture service: %1").arg(hresultText(hr)));
            return false;
        }

        if (FAILED(hr = client->Start())) {
            fail(QStringLiteral("cannot start capture: %1").arg(hresultText(hr)));
            return false;
        }

        // A new stream is a new epoch, so the DSP discards anything it was
        // part-way through analysing at the old rate.
        m_counters->sampleRate.store(rate, std::memory_order_relaxed);
        m_counters->healthy.store(true, std::memory_order_relaxed);
        const std::uint32_t epoch = m_epoch->fetch_add(1, std::memory_order_relaxed) + 1;

        captureLoop(enumerator.get(), capture.get(), kind, channels, rate, epoch, openedId);

        client->Stop();
        m_counters->healthy.store(false, std::memory_order_relaxed);
        return true;
    }

    void captureLoop(IMMDeviceEnumerator *enumerator, IAudioCaptureClient *capture,
                     SampleKind kind, std::uint32_t channels, double rate,
                     std::uint32_t epoch, const QString &openedId)
    {
        // Poll rather than wait on an event. Event-driven loopback needs a
        // silent render stream kept open alongside it to generate the events --
        // a second stream and a second failure mode, for no benefit at this
        // frame rate. Five milliseconds of jitter is invisible at 60 fps.
        constexpr DWORD kPollMs = 5;

        // How long without a packet counts as silence.
        //
        // A loopback stream delivers NOTHING while the endpoint is idle, rather
        // than delivering zeros, and that has no macOS equivalent: the tap
        // there always produces silence, so the silence detector, the idle
        // throttle and the permission probe are all written expecting samples
        // to keep arriving. Turning idle back into zeros here means none of
        // them needs a Windows-specific case.
        constexpr ULONGLONG kSilenceAfterMs = 20;
        const auto silenceFrames =
            static_cast<std::uint32_t>(std::max(1.0, rate * kSilenceAfterMs / 1000.0));

        // Re-check the default endpoint twice a second.
        //
        // IMMNotificationClient reports this without polling, but it is a COM
        // callback object with its own lifetime rules, delivered on an
        // arbitrary thread, and every implementation then has to debounce the
        // notification storm a device change produces. Half a second of latency
        // on an event that happens when someone plugs in headphones buys us out
        // of all of that.
        constexpr ULONGLONG kDefaultCheckMs = 500;

        std::vector<float> mono;
        std::vector<float> interleaved;
        ULONGLONG lastPacket = GetTickCount64();
        ULONGLONG lastDefaultCheck = lastPacket;

        while (!m_quit.load(std::memory_order_relaxed)) {
            UINT32 packetFrames = 0;
            HRESULT hr = capture->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) {
                fail(QStringLiteral("capture stopped: %1").arg(hresultText(hr)));
                return;
            }

            if (packetFrames == 0) {
                const ULONGLONG now = GetTickCount64();

                if (now - lastPacket >= kSilenceAfterMs) {
                    mono.assign(silenceFrames, 0.0f);
                    writeMono(mono.data(), silenceFrames, epoch);
                    lastPacket = now;
                }

                if (m_deviceId.isEmpty() && now - lastDefaultCheck >= kDefaultCheckMs) {
                    lastDefaultCheck = now;
                    const QString current = currentDefaultId(enumerator);
                    if (!current.isEmpty() && current != openedId)
                        return; // rebuild against the new default
                }

                Sleep(kPollMs);
                continue;
            }

            while (packetFrames > 0 && !m_quit.load(std::memory_order_relaxed)) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                hr = capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr);
                if (hr == AUDCLNT_S_BUFFER_EMPTY)
                    break;
                if (FAILED(hr)) {
                    fail(QStringLiteral("capture read failed: %1").arg(hresultText(hr)));
                    return;
                }

                if (frames > 0) {
                    mono.resize(frames);
                    if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        // The endpoint is saying the buffer is silent and its
                        // contents are undefined. Believe the flag; reading it
                        // would be reading uninitialised memory.
                        std::fill(mono.begin(), mono.end(), 0.0f);
                    } else if (kind == SampleKind::Float32) {
                        downmixInterleaved(reinterpret_cast<const float *>(data), mono.data(),
                                           frames, channels);
                    } else {
                        const std::size_t total = static_cast<std::size_t>(frames) * channels;
                        interleaved.resize(total);
                        const auto *pcm = reinterpret_cast<const std::int16_t *>(data);
                        for (std::size_t i = 0; i < total; ++i)
                            interleaved[i] = static_cast<float>(pcm[i]) / 32768.0f;
                        downmixInterleaved(interleaved.data(), mono.data(), frames, channels);
                    }

                    writeMono(mono.data(), frames, epoch);
                    lastPacket = GetTickCount64();
                }

                capture->ReleaseBuffer(frames);

                hr = capture->GetNextPacketSize(&packetFrames);
                if (FAILED(hr)) {
                    fail(QStringLiteral("capture stopped: %1").arg(hresultText(hr)));
                    return;
                }
            }
        }
    }

    void writeMono(float *data, std::uint32_t frames, std::uint32_t epoch)
    {
        // Same order as the macOS callback: sanitize before the ring, never
        // after. One NaN reaching the FFT poisons every band permanently.
        sanitize(data, frames);
        if (!m_ring->write(data, frames, epoch))
            m_counters->overruns.fetch_add(1, std::memory_order_relaxed);
        m_counters->callbacks.fetch_add(1, std::memory_order_relaxed);
    }

    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    bool m_loopback = true;
    QString m_deviceId;
    MonoRing *m_ring = nullptr;
    Counters *m_counters = nullptr;
    std::atomic<std::uint32_t> *m_epoch = nullptr;
    std::function<void(QString)> m_onFailure;
};

class WasapiSource : public AudioSource
{
public:
    explicit WasapiSource(QObject *parent) : AudioSource(parent) {}
    ~WasapiSource() override { stop(); }

    bool start() override
    {
        if (m_running)
            return true;

        startOutput();
        m_running = true;
        if (m_inputEnabled)
            startInput();

        // Loopback needs no permission on Windows: there is no equivalent of
        // the macOS system-audio TCC prompt. The only thing that can stop us is
        // the audio service being unavailable, which surfaces as a failure
        // rather than as a permission state.
        setPermission(PermissionState::Granted);
        return true;
    }

    void stop() override
    {
        m_output.stop();
        m_input.stop();
        m_running = false;
    }

    bool isRunning() const override { return m_running; }

    double sampleRate() const override
    {
        const double rate = m_outputCounters.sampleRate.load(std::memory_order_relaxed);
        return rate > 0.0 ? rate : 48000.0;
    }

    std::uint32_t formatEpoch() const override { return m_epoch.load(std::memory_order_relaxed); }

    PermissionState permissionState() const override { return m_permission; }

    MonoRing *ring() override { return &m_ring; }

    std::uint64_t callbackCount() const override
    {
        return m_outputCounters.callbacks.load(std::memory_order_relaxed);
    }

    std::uint64_t overrunCount() const override
    {
        return m_outputCounters.overruns.load(std::memory_order_relaxed);
    }

    QString statusDetail() const override
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        if (!m_status.isEmpty())
            return m_status;
        if (!m_running)
            return QStringLiteral("not started");
        if (!m_outputCounters.healthy.load(std::memory_order_relaxed))
            return QStringLiteral("opening endpoint");
        return QStringLiteral("WASAPI loopback at %1 Hz").arg(sampleRate());
    }

    AudioDeviceList audioDevices() const override
    {
        AudioDeviceList devices;

        // Enumeration runs on the caller's thread, which is the UI thread and
        // an STA. Initialise per call rather than assuming: the menu can be
        // built before anything else here has touched COM.
        const bool weInitialised = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

        ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), enumerator.putVoid()))
            && enumerator) {
            QString defaultId;
            ComPtr<IMMDevice> defaultDevice;
            if (SUCCEEDED(
                    enumerator->GetDefaultAudioEndpoint(eRender, eConsole, defaultDevice.put()))
                && defaultDevice) {
                LPWSTR id = nullptr;
                if (SUCCEEDED(defaultDevice->GetId(&id)) && id) {
                    defaultId = QString::fromWCharArray(id);
                    CoTaskMemFree(id);
                }
            }

            ComPtr<IMMDeviceCollection> collection;
            if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                                         collection.put()))
                && collection) {
                UINT count = 0;
                collection->GetCount(&count);
                for (UINT i = 0; i < count; ++i) {
                    ComPtr<IMMDevice> device;
                    if (FAILED(collection->Item(i, device.put())) || !device)
                        continue;

                    AudioDevice out;
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(device->GetId(&id)) && id) {
                        out.uid = QString::fromWCharArray(id);
                        CoTaskMemFree(id);
                    }
                    if (out.uid.isEmpty())
                        continue;

                    ComPtr<IPropertyStore> props;
                    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.put())) && props) {
                        PROPVARIANT name;
                        PropVariantInit(&name);
                        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name))
                            && name.vt == VT_LPWSTR && name.pwszVal) {
                            out.name = QString::fromWCharArray(name.pwszVal);
                        }
                        PropVariantClear(&name);
                    }
                    if (out.name.isEmpty())
                        out.name = QStringLiteral("Output device");

                    out.isDefaultOutput = (out.uid == defaultId);
                    devices.push_back(out);
                }
            }
        }

        if (weInitialised)
            CoUninitialize();
        return devices;
    }

    bool setSourceDeviceUid(const QString &uid) override
    {
        if (uid == m_sourceUid)
            return true;
        m_sourceUid = uid;
        if (m_running) {
            m_output.stop();
            startOutput();
        }
        Q_EMIT sourceChanged(uid);
        return true;
    }

    QString sourceDeviceUid() const override { return m_sourceUid; }

    bool setInputEnabled(bool enabled) override
    {
        if (enabled == m_inputEnabled)
            return true;
        m_inputEnabled = enabled;
        if (!m_running)
            return true;
        if (enabled)
            startInput();
        else
            m_input.stop();
        return true;
    }

    bool inputEnabled() const override { return m_inputEnabled; }
    MonoRing *inputRing() override { return &m_inputRing; }

    std::uint64_t inputBacklogFrames() const override { return m_inputRing.available(); }

private:
    void startOutput()
    {
        m_output.start(/*loopback=*/true, m_sourceUid, &m_ring, &m_outputCounters, &m_epoch,
                       [this](QString what) { reportStatus(std::move(what)); });
    }

    void startInput()
    {
        m_input.start(/*loopback=*/false, QString(), &m_inputRing, &m_inputCounters,
                      &m_inputEpoch, [this](QString what) {
                          // The microphone is the one thing here a privacy
                          // setting can refuse, so name it. "Access denied"
                          // with no subject is the worst version of this.
                          reportStatus(QStringLiteral("input: %1").arg(what));
                      });
    }

    void reportStatus(QString what)
    {
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status = what;
        }
        qWarning() << "hyperwave: WASAPI:" << what;
    }

    void setPermission(PermissionState state)
    {
        if (m_permission == state)
            return;
        m_permission = state;
        // Failures arrive on a capture thread; signals belong to the thread
        // that owns this object.
        QMetaObject::invokeMethod(
            this, [this, state] { Q_EMIT permissionStateChanged(static_cast<int>(state)); },
            Qt::QueuedConnection);
    }

    MonoRing m_ring;
    MonoRing m_inputRing;
    CaptureStream m_output;
    CaptureStream m_input;
    CaptureStream::Counters m_outputCounters;
    CaptureStream::Counters m_inputCounters;
    std::atomic<std::uint32_t> m_epoch{0};
    std::atomic<std::uint32_t> m_inputEpoch{0};

    bool m_running = false;
    bool m_inputEnabled = false;
    QString m_sourceUid;
    PermissionState m_permission = PermissionState::Unknown;

    mutable std::mutex m_statusMutex;
    QString m_status;
};

} // namespace

std::unique_ptr<AudioSource> AudioSource::create(QObject *parent)
{
    return std::make_unique<WasapiSource>(parent);
}

} // namespace hyperwave
