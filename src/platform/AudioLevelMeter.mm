#include "platform/AudioLevelMeter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <QDebug>

namespace hyperlight {

namespace {


// Applied once per IO callback. The buffer is deliberately large (see the frame
// size below), so a callback is roughly 85 ms and this gives about a two-second
// tail. The meter answers "has this been making noise lately", which is what the
// menu is really asking; tracking the instantaneous level would make apps
// flicker in and out between beats.
//
// This constant is tied to the buffer size. Change one and the tail length
// changes with it.
constexpr float kDecayPerCallback = 0.96f;

constexpr int kMaxMetered = AudioLevelMeter::kMaxMeteredProcesses;

AudioObjectPropertyAddress addr(AudioObjectPropertySelector selector)
{
    return {selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

} // namespace

struct AudioLevelMeter::Impl
{
    QVector<quint32> processes;   // accepted, in stream order -- indexes peak[]
    // What was last asked for, which is not the same thing: a tap can fail to
    // be created. Comparing against `processes` instead would see a mismatch
    // every time that happened and rebuild the whole aggregate once a second,
    // forever.
    QVector<quint32> requested;
    std::vector<AudioObjectID> taps;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;

    // Written by the IO thread, read by the main thread. Relaxed is right: a
    // menu reading a level one callback out of date is not a defect.
    std::atomic<float> peak[kMaxMetered];

    Impl()
    {
        for (auto &p : peak)
            p.store(0.0f, std::memory_order_relaxed);
    }

    void teardown()
    {
        if (aggregate != kAudioObjectUnknown && ioProc) {
            AudioDeviceStop(aggregate, ioProc);
            AudioDeviceDestroyIOProcID(aggregate, ioProc);
            ioProc = nullptr;
        }
        if (aggregate != kAudioObjectUnknown) {
            AudioHardwareDestroyAggregateDevice(aggregate);
            aggregate = kAudioObjectUnknown;
        }
        for (AudioObjectID tap : taps)
            AudioHardwareDestroyProcessTap(tap);
        taps.clear();
        processes.clear();
        requested.clear();
        for (auto &p : peak)
            p.store(0.0f, std::memory_order_relaxed);
    }
};

namespace {

OSStatus meterProc(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *input,
                   const AudioTimeStamp *, AudioBufferList *output, const AudioTimeStamp *,
                   void *refCon)
{
    // Same rule as the main capture: never hand the HAL a buffer we have not
    // written. This aggregate is tap-only so there should be no output at all,
    // and the memset costs nothing when there is none.
    if (output) {
        for (UInt32 b = 0; b < output->mNumberBuffers; ++b) {
            AudioBuffer &buffer = output->mBuffers[b];
            if (buffer.mData && buffer.mDataByteSize)
                std::memset(buffer.mData, 0, buffer.mDataByteSize);
        }
    }

    auto *impl = static_cast<AudioLevelMeter::Impl *>(refCon);
    if (!impl || !input)
        return noErr;

    // One buffer per tap, in the order the taps were listed.
    const UInt32 count = std::min<UInt32>(input->mNumberBuffers, kMaxMetered);
    for (UInt32 b = 0; b < count; ++b) {
        const AudioBuffer &buffer = input->mBuffers[b];
        const auto *samples = static_cast<const float *>(buffer.mData);
        const UInt32 frames = buffer.mDataByteSize / sizeof(float);

        float rms = 0.0f;
        if (samples && frames) {
            double sum = 0.0;
            for (UInt32 i = 0; i < frames; ++i) {
                const double v = samples[i];
                sum += v * v;
            }
            const double mean = sum / frames;
            if (std::isfinite(mean) && mean > 0.0)
                rms = static_cast<float>(std::sqrt(mean));
        }

        const float previous = impl->peak[b].load(std::memory_order_relaxed);
        impl->peak[b].store(std::max(rms, previous * kDecayPerCallback),
                            std::memory_order_relaxed);
    }

    // Buffers that vanished (a tap went away mid-teardown) must not keep their
    // last value forever.
    for (UInt32 b = count; b < kMaxMetered; ++b)
        impl->peak[b].store(0.0f, std::memory_order_relaxed);

    return noErr;
}

} // namespace

AudioLevelMeter::AudioLevelMeter()
    : m_impl(std::make_unique<Impl>())
{
}

AudioLevelMeter::~AudioLevelMeter()
{
    m_impl->teardown();
}

void AudioLevelMeter::stop()
{
    m_impl->teardown();
}

void AudioLevelMeter::setProcesses(const QVector<quint32> &objectIds)
{
    QVector<quint32> wanted = objectIds;
    if (wanted.size() > kMaxMetered)
        wanted.resize(kMaxMetered);
    std::sort(wanted.begin(), wanted.end());

    QVector<quint32> current = m_impl->requested;
    std::sort(current.begin(), current.end());
    if (wanted == current)
        return;   // nothing to do, and rebuilding would cost far more than this check

    m_impl->teardown();
    if (wanted.isEmpty())
        return;

    // One tap per process, all of them in a single aggregate.
    NSMutableArray *tapList = [NSMutableArray array];
    QVector<quint32> accepted;

    for (quint32 objectId : wanted) {
        CATapDescription *desc = [[CATapDescription alloc]
            initStereoMixdownOfProcesses:@[ @(static_cast<AudioObjectID>(objectId)) ]];
        // The prefix matters: the startup sweep in CoreAudioTapSource destroys
        // orphans by name, and these have to be caught by it too.
        desc.name = [NSString stringWithFormat:@"Hyperlight Meter %u", objectId];
        desc.UUID = [NSUUID UUID];
        desc.privateTap = YES;
        desc.muteBehavior = CATapUnmuted;   // metering must never silence anyone

        AudioObjectID tap = kAudioObjectUnknown;
        if (AudioHardwareCreateProcessTap(desc, &tap) != noErr || tap == kAudioObjectUnknown)
            continue;

        CFStringRef uid = nullptr;
        UInt32 size = sizeof(uid);
        AudioObjectPropertyAddress ua = addr(kAudioTapPropertyUID);
        if (AudioObjectGetPropertyData(tap, &ua, 0, nullptr, &size, &uid) != noErr || !uid) {
            AudioHardwareDestroyProcessTap(tap);
            continue;
        }

        m_impl->taps.push_back(tap);
        accepted.append(objectId);
        [tapList addObject:@{
            @(kAudioSubTapUIDKey) : (__bridge_transfer NSString *)uid,
            @(kAudioSubTapDriftCompensationKey) : @0,
        }];
    }

    if (tapList.count == 0) {
        m_impl->teardown();
        return;
    }

    NSDictionary *composition = @{
        @(kAudioAggregateDeviceUIDKey) :
            [NSString stringWithFormat:@"com.hypernuclear.hyperlight.meter.%@",
                                       [[NSUUID UUID] UUIDString]],
        @(kAudioAggregateDeviceNameKey) : @"Hyperlight Meter",
        @(kAudioAggregateDeviceIsPrivateKey) : @1,
        @(kAudioAggregateDeviceIsStackedKey) : @0,
        @(kAudioAggregateDeviceTapListKey) : tapList,
    };

    AudioObjectID aggregate = kAudioObjectUnknown;
    if (AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)composition, &aggregate)
            != noErr
        || aggregate == kAudioObjectUnknown) {
        m_impl->teardown();
        return;
    }
    m_impl->aggregate = aggregate;
    m_impl->processes = accepted;   // stream order matches tap order
    m_impl->requested = wanted;

    // A big buffer, deliberately.
    //
    // Capture wants small buffers because latency shows on screen. A meter does
    // not: it answers "has this been making noise lately" and is read when a
    // menu opens. Asking for 4096 frames drops the callback rate from ~94/s to
    // ~11/s, and with sixteen taps to deliver each time that is most of what
    // this costs. Failure is non-fatal -- the device keeps whatever it had.
    UInt32 frames = 4096;
    AudioObjectPropertyAddress bs = addr(kAudioDevicePropertyBufferFrameSize);
    (void)AudioObjectSetPropertyData(aggregate, &bs, 0, nullptr, sizeof(frames), &frames);
    if (AudioDeviceCreateIOProcID(aggregate, meterProc, m_impl.get(), &m_impl->ioProc) != noErr
        || !m_impl->ioProc) {
        m_impl->teardown();
        return;
    }
    if (AudioDeviceStart(aggregate, m_impl->ioProc) != noErr) {
        m_impl->teardown();
        return;
    }
    m_impl->requested = wanted;
}

float AudioLevelMeter::levelDb(quint32 objectId) const
{
    const int index = m_impl->processes.indexOf(objectId);
    if (index < 0 || index >= kMaxMetered)
        return -120.0f;
    const float peak = m_impl->peak[index].load(std::memory_order_relaxed);
    if (peak <= 1e-6f)
        return -120.0f;
    return 20.0f * std::log10(peak);
}

bool AudioLevelMeter::isAudible(quint32 objectId) const
{
    return levelDb(objectId) > AudioLevelMeter::kAudibleDb;
}

} // namespace hyperlight
