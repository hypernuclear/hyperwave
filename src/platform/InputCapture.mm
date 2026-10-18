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
#include "audio/Resample.h"

namespace hyperwave {

namespace {

constexpr std::uint32_t kMaxIoFrames = 8192;

// Room for the largest IO buffer upsampled from the slowest likely mic: a
// 16k webcam into a 192k analysis is 12x. Anything beyond is cut, not queued.
constexpr std::uint32_t kMaxResampledFrames = kMaxIoFrames * 12;

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

    // The analysis rate the input is resampled to, and the resulting step
    // (input rate / analysis rate). The step is read on the IO thread; the
    // resampler and its buffer are only ever touched there once IO starts.
    std::atomic<double> targetRate{0.0};
    std::atomic<double> step{1.0};
    LinearResampler resampler;
    std::vector<float> resampled;

    // Following the default input, so a mic that changes underneath -- AirPods
    // connecting, an interface unplugged -- is picked up without a restart.
    bool following = false;
    AudioObjectID rateDevice = kAudioObjectUnknown;
    AudioObjectPropertyListenerBlock onDefaultInput = nil;
    AudioObjectPropertyListenerBlock onRate = nil;

    void updateStep()
    {
        const double in = sampleRate.load(std::memory_order_relaxed);
        const double out = targetRate.load(std::memory_order_relaxed);
        step.store((in > 0.0 && out > 0.0) ? in / out : 1.0, std::memory_order_relaxed);
    }
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
    //
    // At the analysis rate, not the device's. The DSP sums the two sample for
    // sample, so a MacBook Pro's 96k mic in a 48k analysis landed an octave low.
    const double step = impl->step.load(std::memory_order_relaxed);
    if (std::abs(step - 1.0) < 1e-6 || impl->resampled.empty()) {
        impl->ring.write(mono, frames, 0);
        return noErr;
    }
    if (impl->resampler.step() != step)
        impl->resampler.setStep(step);
    const std::uint32_t out = impl->resampler.process(
        mono, frames, impl->resampled.data(), static_cast<std::uint32_t>(impl->resampled.size()));
    if (out > 0)
        impl->ring.write(impl->resampled.data(), out, 0);
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
std::uint64_t InputCapture::backlogFrames() const
{
    return m_impl->ring.available();
}

bool InputCapture::start()
{
    if (isRunning())
        return true;
    if (!open())
        return false;
    follow(true);
    return true;
}

void InputCapture::stop()
{
    follow(false);
    close();
}

void InputCapture::setTargetRate(double rate)
{
    m_impl->targetRate.store(rate, std::memory_order_relaxed);
    m_impl->updateStep();
}

bool InputCapture::open()
{
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
    m_impl->updateStep();
    if (m_impl->resampled.empty())
        m_impl->resampled.resize(kMaxResampledFrames);
    m_impl->resampler.reset();

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

void InputCapture::close()
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

void InputCapture::reopen()
{
    close();
    if (open())
        watchRate();
    else
        qInfo("hyperwave: the new default input cannot be listened to; waiting for another");
}
void InputCapture::watchRate()
{
    // The device's own rate can change under us too (Audio MIDI Setup, or an
    // interface switching modes), and the step has to follow it.
    if (m_impl->rateDevice != kAudioObjectUnknown) {
        AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate);
        AudioObjectRemovePropertyListenerBlock(m_impl->rateDevice, &a, dispatch_get_main_queue(),
                                               m_impl->onRate);
        m_impl->rateDevice = kAudioObjectUnknown;
    }
    if (!m_impl->following || m_impl->device == kAudioObjectUnknown || !isRunning())
        return;
    AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate);
    if (AudioObjectAddPropertyListenerBlock(m_impl->device, &a, dispatch_get_main_queue(),
                                            m_impl->onRate)
        == noErr) {
        m_impl->rateDevice = m_impl->device;
    }
}
void InputCapture::follow(bool on)
{
    // Listeners on the main queue, like the tap's, so start/stop/reopen never
    // race each other.
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDefaultInputDevice);
    if (on && !m_impl->following) {
        m_impl->onDefaultInput = ^(UInt32, const AudioObjectPropertyAddress *) {
            if (!m_impl->following || defaultInputDevice() == m_impl->device)
                return;
            qInfo("hyperwave: default input changed, following it");
            reopen();
        };
        m_impl->onRate = ^(UInt32, const AudioObjectPropertyAddress *) {
            if (m_impl->following)
                reopen();
        };
        if (AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &a,
                                                dispatch_get_main_queue(), m_impl->onDefaultInput)
            == noErr) {
            m_impl->following = true;
        }
        watchRate();
    } else if (!on && m_impl->following) {
        m_impl->following = false;
        watchRate();   // removes the rate listener now that following is off
        AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &a,
                                               dispatch_get_main_queue(), m_impl->onDefaultInput);
        m_impl->onDefaultInput = nil;
        m_impl->onRate = nil;
    }
}
} // namespace hyperwave
