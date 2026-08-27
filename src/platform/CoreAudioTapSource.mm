#include "platform/CoreAudioTapSource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include "audio/Downmix.h"
#include "platform/AudioProcessProbe.h"

namespace hyperwave {

namespace {

constexpr const char *kTapName = "Hyperwave System Audio";

// Every tap this app creates starts with this, so the startup sweep can find
// orphans from a previous run without knowing which kind they were.
constexpr const char *kTapNamePrefix = "Hyperwave";

// Frames of scratch on the realtime stack. A device handing us more than this
// in one callback would mean a buffer longer than 170 ms, which no output
// device does; we clamp rather than allocate, because allocating on the HAL IO
// thread is exactly the thing that must never happen here.
constexpr std::uint32_t kMaxIoFrames = 8192;

AudioObjectPropertyAddress addr(AudioObjectPropertySelector selector,
                                AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    return {selector, scope, kAudioObjectPropertyElementMain};
}

NSString *deviceUid(AudioObjectID device)
{
    if (device == kAudioObjectUnknown)
        return nil;
    CFStringRef uid = nullptr;
    UInt32 size = sizeof(uid);
    AudioObjectPropertyAddress a = addr(kAudioDevicePropertyDeviceUID);
    if (AudioObjectGetPropertyData(device, &a, 0, nullptr, &size, &uid) != noErr || !uid)
        return nil;
    return (__bridge_transfer NSString *)uid;
}

AudioObjectID defaultOutputDevice()
{
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDefaultOutputDevice);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, &device) != noErr)
        return kAudioObjectUnknown;
    return device;
}

double nominalSampleRate(AudioObjectID device)
{
    if (device == kAudioObjectUnknown)
        return 0.0;
    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate);
    if (AudioObjectGetPropertyData(device, &a, 0, nullptr, &size, &rate) != noErr)
        return 0.0;
    return rate;
}

// How many input channels the aggregate is actually offering. Zero means the
// tap-only composition produced nothing to listen to, which is the signal to
// fall back to including the output device.
UInt32 inputChannelCount(AudioObjectID device)
{
    AudioObjectPropertyAddress a =
        addr(kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &a, 0, nullptr, &size) != noErr || size == 0)
        return 0;

    std::vector<std::uint8_t> storage(size);
    auto *list = reinterpret_cast<AudioBufferList *>(storage.data());
    if (AudioObjectGetPropertyData(device, &a, 0, nullptr, &size, list) != noErr)
        return 0;

    UInt32 channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        channels += list->mBuffers[i].mNumberChannels;
    return channels;
}

OSStatus ioProcTrampoline(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *input,
                          const AudioTimeStamp *, AudioBufferList *output, const AudioTimeStamp *,
                          void *refCon)
{
    auto *self = static_cast<CoreAudioTapSource *>(refCon);
    return static_cast<OSStatus>(self->renderCallback(input, output));
}

} // namespace

// Everything that needs a CoreAudio or Objective-C type lives here, so the
// header stays clean C++.
struct CoreAudioTapSource::Impl
{
    AudioObjectID tap = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioObjectID outputDevice = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;
    NSString *tapUid = nil;
    NSString *outputUid = nil;
    std::vector<std::pair<AudioObjectID, AudioObjectPropertyAddress>> listeners;
    std::vector<id> listenerBlocks;
    std::atomic<bool> sawNonZeroAudio{false};
};

CoreAudioTapSource::CoreAudioTapSource(QObject *parent)
    : AudioSource(parent)
    , m_ring(std::make_unique<MonoRing>())
    , m_impl(std::make_unique<Impl>())
{
    // A burst of device notifications arrives over a second or two whenever
    // Bluetooth connects. Rebuilding the aggregate five times in that window is
    // a reliable way to wedge the HAL, so every change coalesces into one
    // rebuild after things have settled.
    m_rebuildDebounce.setSingleShot(true);
    m_rebuildDebounce.setInterval(250);
    connect(&m_rebuildDebounce, &QTimer::timeout, this,
            &CoreAudioTapSource::rebuildAfterHardwareChange);
    // Which processes are worth metering changes as apps open and close audio
    // devices, which is rare. A second is plenty, and setProcesses() is a cheap
    // no-op whenever the membership has not actually moved.
    // Re-enumerating processes is not free: each one costs three
    // AudioObjectGetPropertyData round trips to coreaudiod, and at roughly
    // thirty processes a one-second poll measured as the single most expensive
    // thing the app did -- more than the FFT, the renderer and the metering
    // combined. So it is event-driven, with a slow timer only as a backstop in
    // case a notification is ever missed.
    m_meterRefresh.setSingleShot(true);
    m_meterRefresh.setInterval(400);
    connect(&m_meterRefresh, &QTimer::timeout, this, &CoreAudioTapSource::refreshMeterProcesses);
    m_meterFallback.setInterval(15000);
    connect(&m_meterFallback, &QTimer::timeout, this, &CoreAudioTapSource::refreshMeterProcesses);
}

CoreAudioTapSource::~CoreAudioTapSource()
{
    stop();
}

void CoreAudioTapSource::setPermissionState(PermissionState state)
{
    if (m_permission == state)
        return;
    m_permission = state;
    Q_EMIT permissionStateChanged(static_cast<int>(state));
}

bool CoreAudioTapSource::start()
{
    if (m_running)
        return true;

    m_shuttingDown.store(false, std::memory_order_release);
    sweepOrphanedTaps();

    if (!createTap()) {
        // Whether a missing grant fails here or succeeds and delivers silence
        // is genuinely version-dependent, so this is not treated as proof of
        // denial on its own -- AudioController weighs it against the process
        // probe before accusing anyone.
        setPermissionState(PermissionState::LikelyDenied);
        return false;
    }

    refreshOutputDevice();

    // Probe tap-only FIRST.
    //
    // An aggregate that contains the default output device makes us an output
    // client of the user's speakers, which is a category of risk we would
    // rather not carry at all. Tap-only should work -- the tap carries its own
    // clock -- but "should" is not something to bet someone's hearing on, so we
    // check rather than assume.
    m_isOutputClient = false;
    bool ok = buildAggregate(false) && startIo();

    if (ok) {
        // Give the IOProc a moment to prove it actually fires. A composition
        // that builds but never delivers is indistinguishable from a working
        // one until you wait for it.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (m_callbackCount.load(std::memory_order_relaxed) == 0
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (m_callbackCount.load(std::memory_order_relaxed) == 0) {
            qInfo("hyperwave: tap-only aggregate produced no callbacks, falling back");
            stopIo();
            destroyAggregate();
            ok = false;
        }
    }

    if (!ok) {
        m_isOutputClient = true;
        if (!buildAggregate(true) || !startIo()) {
            m_statusDetail = QStringLiteral("could not build an aggregate device");
            destroyAggregate();
            destroyTap();
            return false;
        }
    }

    m_statusDetail = m_isOutputClient
        ? QStringLiteral("aggregate includes the output device (output client)")
        : QStringLiteral("tap-only aggregate");
    // createTap() may have had something to say about the requested source not
    // being available; it would otherwise be lost to the line above.
    if (!m_sourceNote.isEmpty())
        m_statusDetail += QStringLiteral(" — ") + m_sourceNote;
    else if (!m_sourceDeviceUid.isEmpty())
        m_statusDetail += QStringLiteral(" — device ") + m_sourceDeviceUid;

    installListeners();
    refreshMeterProcesses();
    m_meterFallback.start();
    m_running = true;
    setPermissionState(PermissionState::Probing);
    return true;
}

void CoreAudioTapSource::stop()
{
    if (!m_running && m_impl->tap == kAudioObjectUnknown)
        return;

    // Ordering here is not stylistic. AudioDeviceStop joins the IO thread, so
    // after it returns the callback is guaranteed not to run again; only then
    // is it safe for the ring's storage to go away. Freeing first produces a
    // use-after-free that reproduces only on a loaded machine.
    m_shuttingDown.store(true, std::memory_order_release);
    m_rebuildDebounce.stop();
    m_meterRefresh.stop();
    m_meterFallback.stop();
    m_meter.stop();
    removeListeners();
    stopIo();
    destroyAggregate();
    destroyTap();
    m_running = false;
}

bool CoreAudioTapSource::createTap()
{
    if (m_impl->tap != kAudioObjectUnknown)
        return true;

    CATapDescription *desc = nil;
    m_sourceNote.clear();

    // A chosen device that is no longer connected must not silently become
    // silence. Pick "AirPods", walk away, and the tap is built for a device
    // that is not there: it stays alive, delivers zeroes forever, and looks
    // exactly like a revoked permission or a broken app. Fall back to the whole
    // system mix and say why -- m_sourceNote is read back into m_statusDetail,
    // which is what the HUD and the logs show.
    bool useGlobalTap = m_sourceDeviceUid.isEmpty();
    if (!useGlobalTap) {
        bool present = false;
        for (const AudioDevice &device : AudioProcessProbe::listOutputDevices()) {
            if (device.uid == m_sourceDeviceUid) {
                present = true;
                break;
            }
        }
        if (!present) {
            useGlobalTap = true;
            m_sourceNote = QStringLiteral("device %1 is not connected, using the system mix")
                               .arg(m_sourceDeviceUid);
            qInfo("hyperwave: %s", qPrintable(m_sourceNote));
        }
    }

    if (useGlobalTap) {
        // An empty exclusion list is what makes this a global tap: every
        // process that outputs audio is included, wherever it is going. This is
        // the default, and it follows the default output device as the user
        // changes it.
        //
        // We exclude nothing, because hyperwave never plays anything -- if that
        // ever changes, our own process object belongs in this array or the
        // meter reacts to itself.
        desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
    } else {
        // Everything destined for one device. Excluding nobody, so this is
        // still a whole-system mix, just restricted to one destination -- which
        // on a Mac with a virtual device, an audio interface and three monitors
        // is a meaningful thing to ask for.
        desc = [[CATapDescription alloc] initExcludingProcesses:@[]
                                                   andDeviceUID:m_sourceDeviceUid.toNSString()
                                                     withStream:0];
    }
    desc.name = @(kTapName);
    desc.UUID = [NSUUID UUID];
    desc.privateTap = YES;      // invisible to other apps and to Audio MIDI Setup
    desc.muteBehavior = CATapUnmuted;   // never, ever mute what the user is listening to

    AudioObjectID tap = kAudioObjectUnknown;
    const OSStatus status = AudioHardwareCreateProcessTap(desc, &tap);
    if (status != noErr || tap == kAudioObjectUnknown) {
        m_statusDetail = QStringLiteral("AudioHardwareCreateProcessTap failed (OSStatus %1)")
                             .arg(static_cast<int>(status));
        qWarning("hyperwave: %s", qPrintable(m_statusDetail));
        return false;
    }
    m_impl->tap = tap;

    // Read the UID back off the object rather than reusing desc.UUID. They
    // ought to agree; the object is the one the aggregate will actually match.
    CFStringRef uid = nullptr;
    UInt32 size = sizeof(uid);
    AudioObjectPropertyAddress a = addr(kAudioTapPropertyUID);
    if (AudioObjectGetPropertyData(m_impl->tap, &a, 0, nullptr, &size, &uid) != noErr || !uid) {
        m_statusDetail = QStringLiteral("tap created but has no UID");
        return false;
    }
    m_impl->tapUid = (__bridge_transfer NSString *)uid;
    return true;
}

bool CoreAudioTapSource::buildAggregate(bool includeOutputDevice)
{
    if (!m_impl->tapUid)
        return false;

    NSString *aggUid = [NSString stringWithFormat:@"com.hypernuclear.hyperwave.agg.%@",
                                                  [[NSUUID UUID] UUIDString]];

    NSMutableDictionary *composition = [@{
        @(kAudioAggregateDeviceUIDKey) : aggUid,
        @(kAudioAggregateDeviceNameKey) : @"Hyperwave Analyzer",
        // Private: it does not appear in Sound preferences and it dies with us.
        @(kAudioAggregateDeviceIsPrivateKey) : @1,
        @(kAudioAggregateDeviceIsStackedKey) : @0,
        @(kAudioAggregateDeviceTapListKey) : @[ @{
            @(kAudioSubTapUIDKey) : m_impl->tapUid,
            @(kAudioSubTapDriftCompensationKey) : @0,
        } ],
    } mutableCopy];

    if (includeOutputDevice && m_impl->outputUid.length) {
        composition[@(kAudioAggregateDeviceSubDeviceListKey)] =
            @[ @{@(kAudioSubDeviceUIDKey) : m_impl->outputUid} ];
        composition[@(kAudioAggregateDeviceMainSubDeviceKey)] = m_impl->outputUid;
    }

    AudioObjectID aggregate = kAudioObjectUnknown;
    const OSStatus status =
        AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)composition, &aggregate);
    if (status != noErr || aggregate == kAudioObjectUnknown) {
        qWarning("hyperwave: AudioHardwareCreateAggregateDevice failed (%d)",
                 static_cast<int>(status));
        return false;
    }
    m_impl->aggregate = aggregate;

    if (inputChannelCount(aggregate) == 0) {
        // Built, but with nothing to listen to. Caller falls back.
        destroyAggregate();
        return false;
    }

    const double rate = nominalSampleRate(aggregate);
    m_sampleRate = (rate > 0.0) ? rate : nominalSampleRate(m_impl->outputDevice);
    if (m_sampleRate <= 0.0)
        m_sampleRate = 48000.0;

    return true;
}

bool CoreAudioTapSource::startIo()
{
    if (m_impl->aggregate == kAudioObjectUnknown)
        return false;

    // A smaller buffer means fresher audio. Failure is fine -- the device keeps
    // whatever it had, and the analysis does not care.
    UInt32 frames = 512;
    AudioObjectPropertyAddress bs = addr(kAudioDevicePropertyBufferFrameSize);
    (void)AudioObjectSetPropertyData(m_impl->aggregate, &bs, 0, nullptr, sizeof(frames), &frames);

    if (AudioDeviceCreateIOProcID(m_impl->aggregate, ioProcTrampoline, this, &m_impl->ioProc)
            != noErr
        || !m_impl->ioProc) {
        return false;
    }
    if (AudioDeviceStart(m_impl->aggregate, m_impl->ioProc) != noErr) {
        AudioDeviceDestroyIOProcID(m_impl->aggregate, m_impl->ioProc);
        m_impl->ioProc = nullptr;
        return false;
    }

    m_formatEpoch.fetch_add(1, std::memory_order_release);
    Q_EMIT formatChanged(m_sampleRate, m_formatEpoch.load(std::memory_order_acquire));
    return true;
}

void CoreAudioTapSource::stopIo()
{
    if (m_impl->aggregate == kAudioObjectUnknown || !m_impl->ioProc)
        return;
    AudioDeviceStop(m_impl->aggregate, m_impl->ioProc);
    AudioDeviceDestroyIOProcID(m_impl->aggregate, m_impl->ioProc);
    m_impl->ioProc = nullptr;
}

void CoreAudioTapSource::destroyAggregate()
{
    if (m_impl->aggregate == kAudioObjectUnknown)
        return;
    AudioHardwareDestroyAggregateDevice(m_impl->aggregate);
    m_impl->aggregate = kAudioObjectUnknown;
}

void CoreAudioTapSource::destroyTap()
{
    if (m_impl->tap == kAudioObjectUnknown)
        return;
    AudioHardwareDestroyProcessTap(m_impl->tap);
    m_impl->tap = kAudioObjectUnknown;
    m_impl->tapUid = nil;
}

int CoreAudioTapSource::renderCallback(const void *inputBufferList, void *outputBufferList) noexcept
{
    // ---------------------------------------------------------------------
    // SAFETY FIRST, BEFORE ANYTHING ELSE.
    //
    // If the aggregate had to include the output device then we are an output
    // client, and whatever sits in these buffers is what the user hears. The
    // HAL is not required to hand them over zeroed, so uninitialised heap at
    // full scale into someone's headphones is a real possibility. This memset
    // is the cheapest insurance in the entire project and it runs
    // unconditionally, including on the tap-only path where it is a no-op.
    // ---------------------------------------------------------------------
    if (auto *out = static_cast<AudioBufferList *>(outputBufferList)) {
        for (UInt32 b = 0; b < out->mNumberBuffers; ++b) {
            AudioBuffer &buffer = out->mBuffers[b];
            if (buffer.mData && buffer.mDataByteSize)
                std::memset(buffer.mData, 0, buffer.mDataByteSize);
        }
    }

    m_callbackCount.fetch_add(1, std::memory_order_relaxed);

    if (m_shuttingDown.load(std::memory_order_acquire))
        return 0;

    const auto *in = static_cast<const AudioBufferList *>(inputBufferList);
    if (!in || in->mNumberBuffers == 0)
        return 0;

    // Stack scratch, never a heap allocation: this is a realtime thread.
    float mono[kMaxIoFrames];
    std::uint32_t frames = 0;

    if (in->mNumberBuffers > 1) {
        // Planar: one buffer per channel.
        const UInt32 channels = std::min<UInt32>(in->mNumberBuffers, 16);
        frames = std::min<std::uint32_t>(in->mBuffers[0].mDataByteSize / sizeof(float),
                                         kMaxIoFrames);
        const float *planes[16] = {};
        for (UInt32 c = 0; c < channels; ++c)
            planes[c] = static_cast<const float *>(in->mBuffers[c].mData);
        downmixPlanar(planes, mono, frames, channels);
    } else {
        const AudioBuffer &buffer = in->mBuffers[0];
        const UInt32 channels = buffer.mNumberChannels ? buffer.mNumberChannels : 1;
        frames = std::min<std::uint32_t>(buffer.mDataByteSize / (sizeof(float) * channels),
                                         kMaxIoFrames);
        const auto *src = static_cast<const float *>(buffer.mData);
        if (!src)
            return 0;
        downmixInterleaved(src, mono, frames, channels);
    }

    if (frames == 0)
        return 0;

    // One NaN reaching the FFT poisons every band permanently.
    sanitize(mono, frames);

    if (!m_impl->sawNonZeroAudio.load(std::memory_order_relaxed)
        && !isExactlyZero(mono, frames)) {
        m_impl->sawNonZeroAudio.store(true, std::memory_order_relaxed);
    }

    if (!m_ring->write(mono, frames, m_formatEpoch.load(std::memory_order_relaxed)))
        m_overruns.fetch_add(1, std::memory_order_relaxed);

    return 0;
}

bool CoreAudioTapSource::setInputEnabled(bool enabled)
{
    if (enabled == m_inputEnabled)
        return true;
    m_inputEnabled = enabled;
    if (!enabled) {
        m_input.stop();
        return true;
    }
    // Starting can fail -- no input device, no channels, a refused proc -- and
    // that is reported rather than swallowed, but it is not fatal: the output
    // tap carries on and the visualiser simply hears less.
    if (!m_input.start()) {
        m_inputEnabled = false;
        return false;
    }
    return true;
}

AudioProcessList CoreAudioTapSource::audioProcesses() const
{
    AudioProcessList processes = AudioProcessProbe::listProcesses();
    for (AudioProcess &process : processes) {
        process.levelDb = m_meter.levelDb(process.objectId);
        process.audible = m_meter.isAudible(process.objectId);
    }

    // Collapse each application's processes into the one row a menu can show.
    //
    // A measured level and a displayable identity almost never live on the same
    // process. Chrome's audio is on an unnamed com.google.Chrome.helper; the
    // name and the icon are on com.google.Chrome, which is silent. So take the
    // loudest member of the group and give it to the member that can be named.
    QHash<QString, float> loudest;
    for (const AudioProcess &process : processes) {
        if (process.bundleId.isEmpty())
            continue;
        const QString key = process.appKey();
        const auto it = loudest.find(key);
        if (it == loudest.end())
            loudest.insert(key, process.levelDb);
        else
            *it = std::max(*it, process.levelDb);
    }

    // Prefer the process whose bundle id *is* the application's, because that is
    // the one carrying the real name and the icon; any named member will do
    // otherwise. A group with no named member gets no representative and stays
    // out of the menu entirely -- which is the right answer for the Core Audio
    // driver host, an unnamed helper that re-renders the whole system mix at
    // about -41 dB whenever a virtual output device is in use and would
    // otherwise look like the loudest thing on the machine.
    QHash<QString, qsizetype> representative;
    for (qsizetype i = 0; i < processes.size(); ++i) {
        const AudioProcess &process = processes[i];
        if (process.bundleId.isEmpty() || !process.isNamed())
            continue;
        const QString key = process.appKey();
        const auto it = representative.constFind(key);
        if (it == representative.constEnd() || (process.bundleId == key
                                                && processes[*it].bundleId != key)) {
            representative.insert(key, i);
        }
    }
    // Groups where no process has a name at all still get one if the bundle id
    // belongs to an installed application. Electron apps need this: Cursor
    // registers only com.todesktop....helper processes and never itself, so
    // there is no named process to promote and LaunchServices is the only thing
    // that knows the app is called Cursor. Anything that resolves to nothing --
    // universalaccessd, WebKit's GPU service, the Core Audio driver host --
    // stays out of the menu, which is the right answer for all three.
    QHash<QString, qsizetype> fallback;
    for (qsizetype i = 0; i < processes.size(); ++i) {
        const AudioProcess &process = processes[i];
        const QString key = process.appKey();
        if (key.isEmpty() || representative.contains(key))
            continue;   // a named process already speaks for this application
        if (AudioProcessProbe::appNameForBundleId(key).isEmpty())
            continue;
        const auto it = fallback.constFind(key);
        if (it == fallback.constEnd() || process.objectId < processes[*it].objectId)
            fallback.insert(key, i);
    }
    for (auto it = fallback.cbegin(); it != fallback.cend(); ++it)
        representative.insert(it.key(), it.value());

    QSet<qsizetype> chosen;
    for (auto it = representative.cbegin(); it != representative.cend(); ++it) {
        AudioProcess &process = processes[it.value()];
        process.levelDb = loudest.value(it.key(), process.levelDb);
        process.audible = process.levelDb > AudioLevelMeter::kAudibleDb;
        if (!process.isNamed())
            process.name = AudioProcessProbe::appNameForBundleId(it.key());
        if (process.bundleId != it.key()) {
            // Only reached when an application registered helpers but not
            // itself. "Slack Helper (Renderer)" is not something anyone chose to
            // play; the app it belongs to is.
            static const QRegularExpression tail(QStringLiteral("\\s+Helper\\b.*$"),
                                                 QRegularExpression::CaseInsensitiveOption);
            process.name.remove(tail);
            process.bundleId = it.key();   // the icon belongs to the app, not the helper
        }
        chosen.insert(it.value());
    }

    // Everything that is not a representative is silenced for display purposes,
    // so an application appears once rather than once per helper. The raw level
    // stays put; only the claim to be worth listing is withdrawn.
    for (qsizetype i = 0; i < processes.size(); ++i) {
        if (!chosen.contains(i))
            processes[i].audible = false;
    }
    return processes;
}

AudioDeviceList CoreAudioTapSource::audioDevices() const
{
    return AudioProcessProbe::listOutputDevices();
}

bool CoreAudioTapSource::setSourceDeviceUid(const QString &uid)
{
    if (uid == m_sourceDeviceUid)
        return true;

    m_sourceDeviceUid = uid;

    // Unlike an output-device change, this one really does need the tap
    // rebuilt: which device it follows is baked into the CATapDescription and
    // there is no property to change afterwards. Recreating a tap re-runs the
    // TCC evaluation, but the grant is already in place by the time anyone can
    // reach the source picker, so in practice it is silent.
    if (m_running && !restartCapture()) {
        qWarning("hyperwave: could not switch source, falling back to the system mix");
        m_sourceDeviceUid.clear();
        restartCapture();
        Q_EMIT sourceChanged(m_sourceDeviceUid);
        return false;
    }

    Q_EMIT sourceChanged(m_sourceDeviceUid);
    return true;
}

bool CoreAudioTapSource::restartCapture()
{
    stop();
    return start();
}

void CoreAudioTapSource::refreshMeterProcesses()
{
    // Meter every process an application owns, not just the one wearing its name.
    //
    // Chrome is the case that forced this, and it is worth being precise about
    // because the same mistake has now been made three different ways. With a
    // YouTube tab playing, a tap on com.google.Chrome.helper -- pid only, no
    // application name, the kind of process the old filter dropped on sight --
    // read -17.0 dB. A tap on com.google.Chrome read -120. A tap on the *named*
    // "Google Chrome Helper" read -120. A global tap minus Spotify read -16.9,
    // which is the same signal: Chrome's audio was fully attributable the whole
    // time, just not to a process anyone would recognise.
    //
    // So candidates are grouped by application and every member is metered.
    // Names and icons still come from the named process, levels from whichever
    // process is actually producing samples, and neither one alone could have
    // answered "is Chrome playing".
    AudioProcessList processes = AudioProcessProbe::listProcesses();
    processes.removeIf([](const AudioProcess &p) { return p.bundleId.isEmpty(); });

    // There are more processes than tap slots -- about fifty against forty --
    // so the order decides who is visible at all.
    //
    // Note what is *not* here: kAudioProcessPropertyIsRunningOutput. It was the
    // first tier until a run of this showed what that costs. Apple's background
    // machinery holds output streams open permanently -- universalaccessd,
    // loginwindow, PowerChime, cloudpaird all claim one and none of them will
    // ever be the answer to "what is playing music" -- so promoting stream
    // holders handed the top of the list to daemons and pushed real
    // applications, Cursor and Audio Hijack's daemon among them, off the end.
    // That property has now been wrong as a filter in both directions and wrong
    // as a priority; it is not consulted here at all.
    //
    // Third-party applications first, because they are the entire question.
    // Then Apple's own media apps, because Music and QuickTime obviously can be
    // the answer. Then everything else Apple ships, on whatever is left.
    const auto priority = [](const AudioProcess &p) {
        if (!p.bundleId.startsWith(QLatin1String("com.apple.")))
            return 0;
        static const QStringList kAppleMedia = {
            QStringLiteral("com.apple.Music"),      QStringLiteral("com.apple.iTunes"),
            QStringLiteral("com.apple.TV"),         QStringLiteral("com.apple.podcasts"),
            QStringLiteral("com.apple.QuickTimePlayerX"), QStringLiteral("com.apple.Safari"),
        };
        return kAppleMedia.contains(p.bundleId) ? 1 : 2;
    };
    struct Group
    {
        QVector<quint32> objectIds;
        int priority = 3;
        bool named = false;
        quint32 first = std::numeric_limits<quint32>::max();
    };
    QHash<QString, Group> groups;
    for (const AudioProcess &process : processes) {
        Group &group = groups[process.appKey()];
        group.objectIds.append(process.objectId);
        group.priority = std::min(group.priority, priority(process));
        group.named = group.named || process.isNamed();
        group.first = std::min(group.first, process.objectId);
    }
    // A group that cannot be named cannot be shown, so it must never hold a tap
    // that a real application needs.
    //
    // This is emphatically not the filter that hid Chrome. That one asked
    // whether the *process* had a name, and Chrome's audio comes out of one
    // that does not. This asks whether the *application* has a name, which is a
    // different question and one LaunchServices can answer from the bundle id
    // even when every process in the group is faceless -- which is exactly the
    // Electron case: Cursor and Audio Hijack's daemon register only helpers,
    // and before the lookup was added both were being turned away for want of a
    // slot despite being ordinary third-party apps. What is left over here is
    // the genuinely anonymous: systemsoundserverd, WebKit's shared GPU service,
    // the Core Audio driver host. None of them can appear in a menu, so none of
    // them should cost a tap.
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (!it->named)
            it->named = !AudioProcessProbe::appNameForBundleId(it.key()).isEmpty();
        if (!it->named)
            it->priority = 3;
    }

    // Whole applications, in priority order. Keeping a group together matters:
    // metering half of Chrome is how this went wrong in the first place.
    QVector<QString> keys = groups.keys();
    std::sort(keys.begin(), keys.end(), [&groups](const QString &a, const QString &b) {
        const Group &ga = groups[a], &gb = groups[b];
        if (ga.priority != gb.priority)
            return ga.priority < gb.priority;
        return ga.first < gb.first;
    });

    QVector<quint32> candidates;
    QStringList metered, dropped;
    for (const QString &key : keys) {
        Group &group = groups[key];
        std::sort(group.objectIds.begin(), group.objectIds.end());
        if (candidates.size() + group.objectIds.size() > AudioLevelMeter::kMaxMeteredProcesses) {
            dropped << key;
            continue;
        }
        candidates.append(group.objectIds);
        metered << QStringLiteral("%1(%2)").arg(key).arg(group.objectIds.size());
    }
    m_meter.setProcesses(candidates);

    if (qEnvironmentVariableIsSet("HYPERWAVE_METER_DEBUG")) {
        qInfo("hyperwave: metering %d process(es) across %d app(s): %s", int(candidates.size()),
              int(metered.size()), qPrintable(metered.join(QStringLiteral(", "))));
        // Never drop coverage silently: an app missing from the "Playing" list
        // because it lost a race for a tap slot looks exactly like a bug in the
        // meter, and this is the line that tells the two apart.
        if (!dropped.isEmpty()) {
            qInfo("hyperwave: no tap slots left for %d app(s): %s", int(dropped.size()),
                  qPrintable(dropped.join(QStringLiteral(", "))));
        }
    }
}
void CoreAudioTapSource::refreshOutputDevice()
{
    m_impl->outputDevice = defaultOutputDevice();
    m_impl->outputUid = deviceUid(m_impl->outputDevice);
}

void CoreAudioTapSource::noteHardwareChange()
{
    if (!m_running || m_shuttingDown.load(std::memory_order_acquire))
        return;
    m_rebuildDebounce.start();
}

void CoreAudioTapSource::rebuildAfterHardwareChange()
{
    if (!m_running || m_shuttingDown.load(std::memory_order_acquire))
        return;

    const AudioObjectID previousDevice = m_impl->outputDevice;
    const double previousRate = m_sampleRate;
    refreshOutputDevice();
    const double newRate = nominalSampleRate(m_impl->outputDevice);

    if (m_impl->outputDevice == previousDevice && std::abs(newRate - previousRate) < 1.0)
        return;   // a duplicate notification, nothing actually moved

    qInfo("hyperwave: output device changed, rebuilding aggregate (%.0f -> %.0f Hz)",
          previousRate, newRate);

    // Only the aggregate is rebuilt. The global tap is not bound to a device,
    // and recreating it risks re-running the TCC evaluation for no reason.
    removeListeners();
    stopIo();
    destroyAggregate();

    m_isOutputClient = false;
    if (!(buildAggregate(false) && startIo())) {
        m_isOutputClient = true;
        if (!(buildAggregate(true) && startIo())) {
            m_statusDetail = QStringLiteral("lost the audio device and could not rebuild");
            m_running = false;
            return;
        }
    }
    installListeners();
}

void CoreAudioTapSource::installListeners()
{
    // Every listener is registered on the main queue rather than with the C
    // callback form. The C form fires on an arbitrary HAL thread and each
    // handler would then need its own hop back to main; registering on the main
    // queue removes that whole class of race up front.
    auto add = [this](AudioObjectID object, AudioObjectPropertySelector selector) {
        if (object == kAudioObjectUnknown)
            return;
        AudioObjectPropertyAddress a = addr(selector);
        AudioObjectPropertyListenerBlock block =
            ^(UInt32, const AudioObjectPropertyAddress *) { this->noteHardwareChange(); };
        if (AudioObjectAddPropertyListenerBlock(object, &a, dispatch_get_main_queue(), block)
            == noErr) {
            m_impl->listeners.emplace_back(object, a);
            m_impl->listenerBlocks.push_back([block copy]);
        }
    };

    // A process appearing or disappearing is what changes the metering set --
    // Chrome spawning its audio helper when a video starts, for instance.
    {
        AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyProcessObjectList);
        AudioObjectPropertyListenerBlock block =
            ^(UInt32, const AudioObjectPropertyAddress *) { m_meterRefresh.start(); };
        if (AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &a,
                                                dispatch_get_main_queue(), block)
            == noErr) {
            m_impl->listeners.emplace_back(kAudioObjectSystemObject, a);
            m_impl->listenerBlocks.push_back([block copy]);
        }
    }
    add(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice);
    add(kAudioObjectSystemObject, kAudioHardwarePropertyDevices);
    add(m_impl->outputDevice, kAudioDevicePropertyNominalSampleRate);
    add(m_impl->aggregate, kAudioDevicePropertyDeviceIsAlive);
    add(m_impl->tap, kAudioTapPropertyFormat);
}

void CoreAudioTapSource::removeListeners()
{
    for (std::size_t i = 0; i < m_impl->listeners.size(); ++i) {
        auto &entry = m_impl->listeners[i];
        auto block = static_cast<AudioObjectPropertyListenerBlock>(m_impl->listenerBlocks[i]);
        AudioObjectRemovePropertyListenerBlock(entry.first, &entry.second,
                                               dispatch_get_main_queue(), block);
    }
    m_impl->listeners.clear();
    m_impl->listenerBlocks.clear();
}

void CoreAudioTapSource::sweepOrphanedTaps()
{
    // Private taps should die with the process, but if a crash ever leaves one
    // behind the user gets an Audio MIDI Setup full of our devices and files a
    // bug about it. Twenty lines of insurance against an embarrassing report.
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyTapList);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr
        || size == 0) {
        return;
    }

    std::vector<AudioObjectID> taps(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, taps.data())
        != noErr) {
        return;
    }

    for (AudioObjectID tap : taps) {
        CATapDescription *desc = nil;
        UInt32 descSize = sizeof(desc);
        AudioObjectPropertyAddress da = addr(kAudioTapPropertyDescription);
        if (AudioObjectGetPropertyData(tap, &da, 0, nullptr, &descSize, &desc) != noErr || !desc)
            continue;
        if ([desc.name hasPrefix:@"Hyperwave"]) {
            qInfo("hyperwave: destroying an orphaned tap from a previous run");
            AudioHardwareDestroyProcessTap(tap);
        }
    }
}

std::unique_ptr<AudioSource> AudioSource::create(QObject *parent)
{
    return std::make_unique<CoreAudioTapSource>(parent);
}

} // namespace hyperwave
