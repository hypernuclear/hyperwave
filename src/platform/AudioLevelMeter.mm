#include "platform/AudioLevelMeter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QString>

namespace hyperwave {

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
    // A tap and the UID the aggregate composition names it by. The UID is kept
    // rather than re-read because kAudioTapPropertyUID is another round trip to
    // coreaudiod for something that cannot change for the life of the tap.
    struct Tap
    {
        AudioObjectID object = kAudioObjectUnknown;
        QString uid;
    };

    // Serialises setProcesses() against stop(). The refresh runs on the meter
    // thread and shutdown arrives from the main one, and two threads inside
    // AudioHardwareDestroyProcessTap is a crash rather than a wrong number.
    std::mutex ops;
    // Latched off by stop(). See the header: without it a refresh already
    // queued when the source shut down would rebuild the whole tap set
    // immediately afterwards and leave it running.
    bool enabled = true;

    QHash<quint32, Tap> taps;     // objectId -> tap, the set that survives a rebuild
    QVector<quint32> processes;   // accepted, in stream order -- indexes peak[]
    // What was last asked for, which is not the same thing: a tap can fail to
    // be created. Comparing against `processes` instead would see a mismatch
    // every time that happened and rebuild the whole aggregate once a second,
    // forever.
    QVector<quint32> requested;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;

    // Guards `processes` and nothing else. levelDb() reads it from the main
    // thread while a rebuild reassigns it on the meter thread, and it is held
    // for the length of a lookup rather than for the rebuild, so a menu opening
    // mid-rebuild waits on a vector search and not on coreaudiod.
    mutable std::mutex map;

    // Written by the IO thread, read by the main thread. Relaxed is right: a
    // menu reading a level one callback out of date is not a defect.
    std::atomic<float> peak[kMaxMetered];

    Impl()
    {
        for (auto &p : peak)
            p.store(0.0f, std::memory_order_relaxed);
    }

    void destroyAggregate()
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
    }

    void destroyTaps()
    {
        for (const Tap &tap : std::as_const(taps))
            AudioHardwareDestroyProcessTap(tap.object);
        taps.clear();
    }

    void teardown()
    {
        // The aggregate goes first. Its composition names every tap, and
        // destroying a tap out from under a live aggregate is the one ordering
        // the HAL does not forgive.
        destroyAggregate();
        destroyTaps();
        {
            const std::lock_guard<std::mutex> lock(map);
            processes.clear();
        }
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
    // Unlocked on purpose: by the time the meter is destroyed the owner has
    // joined the meter thread, so there is nobody left to race with, and in the
    // ordinary path stop() has already left this with nothing to do.
    m_impl->teardown();
}

void AudioLevelMeter::stop()
{
    const std::lock_guard<std::mutex> lock(m_impl->ops);
    m_impl->enabled = false;
    m_impl->teardown();
}

void AudioLevelMeter::setEnabled(bool enabled)
{
    const std::lock_guard<std::mutex> lock(m_impl->ops);
    m_impl->enabled = enabled;
}

void AudioLevelMeter::setProcesses(const QVector<quint32> &objectIds)
{
    QVector<quint32> wanted = objectIds;
    if (wanted.size() > kMaxMetered)
        wanted.resize(kMaxMetered);
    std::sort(wanted.begin(), wanted.end());

    const std::lock_guard<std::mutex> lock(m_impl->ops);
    if (!m_impl->enabled)
        return;

    // `requested` is only ever stored sorted, so this compares like with like.
    if (wanted == m_impl->requested)
        return;   // nothing to do, and rebuilding would cost far more than this check

    if (wanted.isEmpty()) {
        m_impl->teardown();
        return;
    }

    // Only the difference is rebuilt.
    //
    // This used to destroy every tap and create every tap again on any change
    // to the membership, and the membership changes whenever an application
    // launches, because launching one registers it with CoreAudio. Each
    // AudioHardwareCreateProcessTap is a synchronous round trip to coreaudiod,
    // so opening an app -- which adds one group to a set of forty -- paid for
    // all forty.
    //
    // Measured on a machine holding the full forty taps, with the phase
    // breakdown the debug line below prints:
    //
    //   full rebuild    2291 ms   taps 1319, aggregate  863, start 109
    //   full rebuild    1788 ms   taps  560, aggregate 1089, start 139
    //   one in, one out 1087 ms   taps  387, aggregate  499, start 183
    //   one in, one out  998 ms   taps  368, aggregate  515, start  97
    //
    // Which is worth reading carefully, because it says the diff is only half
    // the story. It reliably removes about half the time, and that half is the
    // part that scaled with the size of the set. What is left does not: the
    // aggregate has to be destroyed and created either way -- its composition
    // names the taps and there is no property to edit that list in place -- and
    // coreaudiod charges hundreds of milliseconds for that whatever else is
    // going on. Between runs the two phases trade places. Anyone chasing the
    // rest should start with the aggregate and not with the tap loop.
    //
    // A second is still not something to do on the main thread, which is why
    // CoreAudioTapSource runs this on one of its own.
    QElapsedTimer elapsed;
    elapsed.start();
    int destroyed = 0;
    int created = 0;

    m_impl->destroyAggregate();
    const qint64 destroyAggregateMs = elapsed.elapsed();

    for (auto it = m_impl->taps.begin(); it != m_impl->taps.end();) {
        if (std::binary_search(wanted.cbegin(), wanted.cend(), it.key())) {
            ++it;
            continue;
        }
        AudioHardwareDestroyProcessTap(it->object);
        ++destroyed;
        it = m_impl->taps.erase(it);
    }

    for (quint32 objectId : std::as_const(wanted)) {
        if (m_impl->taps.contains(objectId))
            continue;

        CATapDescription *desc = [[CATapDescription alloc]
            initStereoMixdownOfProcesses:@[ @(static_cast<AudioObjectID>(objectId)) ]];
        // The prefix matters: the startup sweep in CoreAudioTapSource destroys
        // orphans by name, and these have to be caught by it too.
        desc.name = [NSString stringWithFormat:@"Hyperwave Meter %u", objectId];
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

        m_impl->taps.insert(objectId,
                            Impl::Tap{tap, QString::fromNSString((__bridge_transfer NSString *)uid)});
        ++created;
    }

    const qint64 tapsMs = elapsed.elapsed();

    // Stream order is `wanted` order, filtered to the taps that exist. Building
    // it from the sorted request rather than from the hash keeps the order
    // stable and reproducible, which matters because it is what indexes peak[].
    NSMutableArray *tapList = [NSMutableArray array];
    QVector<quint32> accepted;
    for (quint32 objectId : std::as_const(wanted)) {
        const auto it = m_impl->taps.constFind(objectId);
        if (it == m_impl->taps.constEnd())
            continue;
        accepted.append(objectId);
        [tapList addObject:@{
            @(kAudioSubTapUIDKey) : it->uid.toNSString(),
            @(kAudioSubTapDriftCompensationKey) : @0,
        }];
    }

    if (tapList.count == 0) {
        m_impl->teardown();
        return;
    }

    NSDictionary *composition = @{
        @(kAudioAggregateDeviceUIDKey) :
            [NSString stringWithFormat:@"com.hypernuclear.hyperwave.meter.%@",
                                       [[NSUUID UUID] UUIDString]],
        @(kAudioAggregateDeviceNameKey) : @"Hyperwave Meter",
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
    const qint64 createAggregateMs = elapsed.elapsed();

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

    // Carry the levels across the reindex.
    //
    // Now that taps survive a rebuild, the applications behind them survive it
    // too, and they should not lose their measurement because something else
    // launched and shifted them one slot along. This runs before AudioDeviceStart
    // and after the old IOProc was stopped, so nothing is writing peak[] while
    // it is being permuted.
    {
        float carried[kMaxMetered] = {};
        for (qsizetype i = 0; i < accepted.size(); ++i) {
            const qsizetype previous = m_impl->processes.indexOf(accepted[i]);
            carried[i] = previous >= 0
                ? m_impl->peak[previous].load(std::memory_order_relaxed)
                : 0.0f;
        }
        const std::lock_guard<std::mutex> mapLock(m_impl->map);
        for (qsizetype i = 0; i < kMaxMetered; ++i) {
            m_impl->peak[i].store(i < accepted.size() ? carried[i] : 0.0f,
                                  std::memory_order_relaxed);
        }
        m_impl->processes = accepted;   // stream order matches tap order
    }

    if (AudioDeviceStart(aggregate, m_impl->ioProc) != noErr) {
        m_impl->teardown();
        return;
    }
    m_impl->requested = wanted;

    // The number worth watching is `created`, not `holding`. The whole point of
    // the diff is that an application launching costs a tap or two rather than
    // the whole set, and this is the line that shows whether it still does.
    if (qEnvironmentVariableIsSet("HYPERWAVE_METER_DEBUG")) {
        qInfo("hyperwave: meter rebuilt in %lld ms (+%d tap(s), -%d, holding %lld) "
              "[destroy %lld, taps %lld, create %lld, start %lld]",
              elapsed.elapsed(), created, destroyed, static_cast<long long>(accepted.size()),
              destroyAggregateMs, tapsMs - destroyAggregateMs,
              createAggregateMs - tapsMs, elapsed.elapsed() - createAggregateMs);
    }
}

float AudioLevelMeter::levelDb(quint32 objectId) const
{
    qsizetype index = -1;
    {
        // Held for a lookup, not for the read: peak[] is atomic and safe to
        // touch outside, and the only thing that can move underneath a caller
        // is the index map itself.
        const std::lock_guard<std::mutex> lock(m_impl->map);
        index = m_impl->processes.indexOf(objectId);
    }
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

} // namespace hyperwave
