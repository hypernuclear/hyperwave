#include "platform/InputCapture.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#import <CoreAudio/AudioHardware.h>
#import <Foundation/Foundation.h>

#include <QDebug>

#include "audio/Downmix.h"

namespace hyperwave {

namespace {

constexpr std::uint32_t kMaxIoFrames = 8192;

AudioObjectPropertyAddress addr(AudioObjectPropertySelector selector,
                                AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    return {selector, scope, kAudioObjectPropertyElementMain};
}

AudioObjectID defaultInputDevice()
{
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDefaultInputDevice);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, &device)
        != noErr) {
        return kAudioObjectUnknown;
    }
    return device;
}

} // namespace

struct InputCapture::Impl
{
    MonoRing ring;
    AudioObjectID device = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = nullptr;
    std::atomic<double> sampleRate{0.0};
    std::atomic<bool> running{false};
    std::atomic<bool> shuttingDown{false};
};

namespace {

OSStatus inputProc(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *input,
                   const AudioTimeStamp *, AudioBufferList *output, const AudioTimeStamp *,
                   void *refCon)
{
    // Never hand back a buffer we have not written. An input-only device should
    // not present one at all, and the memset costs nothing when there is none.
    if (output) {
        for (UInt32 b = 0; b < output->mNumberBuffers; ++b) {
            AudioBuffer &buffer = output->mBuffers[b];
            if (buffer.mData && buffer.mDataByteSize)
                std::memset(buffer.mData, 0, buffer.mDataByteSize);
        }
    }

    auto *impl = static_cast<InputCapture::Impl *>(refCon);
    if (!impl || impl->shuttingDown.load(std::memory_order_acquire))
        return noErr;
    if (!input || input->mNumberBuffers == 0)
        return noErr;

    float mono[kMaxIoFrames];
    std::uint32_t frames = 0;

    if (input->mNumberBuffers > 1) {
        const UInt32 channels = std::min<UInt32>(input->mNumberBuffers, 16);
        frames = std::min<std::uint32_t>(input->mBuffers[0].mDataByteSize / sizeof(float),
                                         kMaxIoFrames);
        const float *planes[16] = {};
        for (UInt32 c = 0; c < channels; ++c)
            planes[c] = static_cast<const float *>(input->mBuffers[c].mData);
        downmixPlanar(planes, mono, frames, channels);
    } else {
        const AudioBuffer &buffer = input->mBuffers[0];
        const UInt32 channels = buffer.mNumberChannels ? buffer.mNumberChannels : 1;
        frames = std::min<std::uint32_t>(buffer.mDataByteSize / (sizeof(float) * channels),
                                         kMaxIoFrames);
        const auto *src = static_cast<const float *>(buffer.mData);
        if (!src)
            return noErr;
        downmixInterleaved(src, mono, frames, channels);
    }

    if (frames == 0)
        return noErr;

    // One NaN reaching the FFT poisons every band permanently, and an interface
    // that has just been unplugged is a good way to produce one.
    sanitize(mono, frames);

    // Epoch zero: the input ring is summed into the analysis rather than
    // driving it, so it has no say in when the format is considered to have
    // changed. Its writes must never invalidate the output ring's epoch.
    impl->ring.write(mono, frames, 0);
    return noErr;
}

} // namespace

InputCapture::InputCapture()
    : m_impl(std::make_unique<Impl>())
{
}

InputCapture::~InputCapture()
{
    stop();
}

bool InputCapture::isRunning() const
{
    return m_impl->running.load(std::memory_order_relaxed);
}

double InputCapture::sampleRate() const
{
    return m_impl->sampleRate.load(std::memory_order_relaxed);
}

MonoRing *InputCapture::ring()
{
    return &m_impl->ring;
}

bool InputCapture::start()
{
    if (isRunning())
        return true;

    m_impl->device = defaultInputDevice();
    if (m_impl->device == kAudioObjectUnknown) {
        qInfo("hyperwave: no input device to listen to");
        return false;
    }

    // The device has to actually have input channels. The default input can be
    // a device that lost them -- an interface in a different mode, a virtual
    // device configured output-only -- and starting an IO proc on it succeeds
    // while delivering nothing at all.
    AudioObjectPropertyAddress ca{kAudioDevicePropertyStreamConfiguration,
                                  kAudioDevicePropertyScopeInput,
                                  kAudioObjectPropertyElementMain};
    UInt32 configSize = 0;
    if (AudioObjectGetPropertyDataSize(m_impl->device, &ca, 0, nullptr, &configSize) != noErr
        || configSize == 0) {
        qInfo("hyperwave: the default input device reports no input streams");
        return false;
    }
    std::vector<std::uint8_t> storage(configSize);
    auto *list = reinterpret_cast<AudioBufferList *>(storage.data());
    UInt32 channels = 0;
    if (AudioObjectGetPropertyData(m_impl->device, &ca, 0, nullptr, &configSize, list) == noErr) {
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
            channels += list->mBuffers[i].mNumberChannels;
    }
    if (channels == 0) {
        qInfo("hyperwave: the default input device has no input channels");
        return false;
    }

    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    AudioObjectPropertyAddress ra = addr(kAudioDevicePropertyNominalSampleRate);
    if (AudioObjectGetPropertyData(m_impl->device, &ra, 0, nullptr, &size, &rate) == noErr)
        m_impl->sampleRate.store(rate, std::memory_order_relaxed);

    m_impl->ring.reset();
    m_impl->shuttingDown.store(false, std::memory_order_release);

    if (AudioDeviceCreateIOProcID(m_impl->device, inputProc, m_impl.get(), &m_impl->proc) != noErr
        || !m_impl->proc) {
        qWarning("hyperwave: could not create an input IO proc");
        m_impl->proc = nullptr;
        return false;
    }
    if (AudioDeviceStart(m_impl->device, m_impl->proc) != noErr) {
        AudioDeviceDestroyIOProcID(m_impl->device, m_impl->proc);
        m_impl->proc = nullptr;
        qWarning("hyperwave: could not start input capture");
        return false;
    }

    m_impl->running.store(true, std::memory_order_relaxed);
    qInfo("hyperwave: listening to the input device as well (%.0f Hz, %u ch)", rate, channels);
    return true;
}

void InputCapture::stop()
{
    if (!m_impl->proc) {
        m_impl->running.store(false, std::memory_order_relaxed);
        return;
    }
    m_impl->shuttingDown.store(true, std::memory_order_release);
    AudioDeviceStop(m_impl->device, m_impl->proc);
    AudioDeviceDestroyIOProcID(m_impl->device, m_impl->proc);
    m_impl->proc = nullptr;
    m_impl->running.store(false, std::memory_order_relaxed);
    m_impl->sampleRate.store(0.0, std::memory_order_relaxed);
}

} // namespace hyperwave
