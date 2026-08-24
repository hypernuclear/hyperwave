#include "platform/AudioProcessProbe.h"

#include <algorithm>
#include <unistd.h>
#include <vector>

#include <QHash>

#import <AppKit/AppKit.h>
#import <CoreAudio/AudioHardware.h>

namespace hyperlight::AudioProcessProbe {

namespace {

AudioObjectPropertyAddress addr(AudioObjectPropertySelector selector)
{
    return {selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

std::vector<AudioObjectID> processObjects(bool *ok)
{
    if (ok)
        *ok = false;

    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyProcessObjectList);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
        return {};

    std::vector<AudioObjectID> objects(size / sizeof(AudioObjectID));
    if (objects.empty())
        return {};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, objects.data())
        != noErr) {
        return {};
    }

    if (ok)
        *ok = true;
    return objects;
}

bool readPid(AudioObjectID object, pid_t &out)
{
    UInt32 size = sizeof(out);
    AudioObjectPropertyAddress a = addr(kAudioProcessPropertyPID);
    return AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, &out) == noErr;
}

bool readIsPlaying(AudioObjectID object)
{
    UInt32 running = 0;
    UInt32 size = sizeof(running);
    AudioObjectPropertyAddress a = addr(kAudioProcessPropertyIsRunningOutput);
    return AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, &running) == noErr && running;
}

// pid -> localised application name, cached.
//
// NSRunningApplication::runningApplicationWithProcessIdentifier: goes to
// LaunchServices, and it is not cheap: profiling the app showed this one lookup
// dominating everything else it does, ahead of the FFT, the renderer and the
// per-application metering combined, purely because it ran for every process
// once a second. Names do not change for the life of a process, so ask once.
QString cachedName(pid_t pid)
{
    static QHash<qint64, QString> cache;
    const auto key = static_cast<qint64>(pid);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    QString name;
    if (NSRunningApplication *app =
            [NSRunningApplication runningApplicationWithProcessIdentifier:pid]) {
        name = QString::fromNSString(app.localizedName);
    }
    // Cached even when empty -- "this pid is not an application" is exactly the
    // answer that would otherwise be recomputed every second for every daemon.
    // Pids do get reused, but only after a process exits, and a stale name on a
    // recycled pid is a cosmetic error in a menu rather than a correctness one.
    if (cache.size() > 512)
        cache.clear();
    cache.insert(key, name);
    return name;
}
QString readBundleId(AudioObjectID object)
{
    CFStringRef bundle = nullptr;
    UInt32 size = sizeof(bundle);
    AudioObjectPropertyAddress a = addr(kAudioProcessPropertyBundleID);
    if (AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, &bundle) != noErr || !bundle)
        return {};
    NSString *value = (__bridge_transfer NSString *)bundle;
    return QString::fromNSString(value);
}

} // namespace

QString appNameForBundleId(const QString &bundleId)
{
    if (bundleId.isEmpty())
        return {};
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(bundleId);
    if (it != cache.constEnd())
        return *it;
    QString name;
    NSString *identifier = bundleId.toNSString();
    if (NSURL *url = [[NSWorkspace sharedWorkspace]
            URLForApplicationWithBundleIdentifier:identifier]) {
        name = QString::fromNSString([[NSFileManager defaultManager] displayNameAtPath:url.path]);
        if (name.endsWith(QLatin1String(".app")))
            name.chop(4);
    }
    // Cached even when empty: "there is no application with this bundle id" is
    // the answer for every daemon on the system and the one most often asked.
    cache.insert(bundleId, name);
    return name;
}
bool isAvailable()
{
    bool ok = false;
    (void)processObjects(&ok);
    return ok;
}

AudioProcessList listProcesses()
{
    bool ok = false;
    const std::vector<AudioObjectID> objects = processObjects(&ok);
    if (!ok)
        return {};

    const pid_t self = getpid();
    AudioProcessList result;
    result.reserve(static_cast<int>(objects.size()));

    for (AudioObjectID object : objects) {
        pid_t pid = 0;
        if (!readPid(object, pid) || pid == self)
            continue;

        AudioProcess process;
        process.objectId = static_cast<quint32>(object);
        process.pid = pid;
        process.bundleId = readBundleId(object);
        process.playingOutput = readIsPlaying(object);

        // NSRunningApplication is what turns "com.spotify.client" into
        // "Spotify". Faceless helpers have no running application and keep an
        // empty name, which is how callers filter them out.
        process.name = cachedName(pid);

        result.append(process);
    }
    return result;
}

AudioDeviceList listOutputDevices()
{
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
        return {};

    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (devices.empty())
        return {};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, devices.data())
        != noErr) {
        return {};
    }

    AudioObjectID defaultOutput = kAudioObjectUnknown;
    UInt32 defaultSize = sizeof(defaultOutput);
    AudioObjectPropertyAddress da = addr(kAudioHardwarePropertyDefaultOutputDevice);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &da, 0, nullptr, &defaultSize,
                               &defaultOutput);

    AudioDeviceList result;
    for (AudioObjectID device : devices) {
        // Output channels are the whole test. Microphones and the input half of
        // duplex interfaces have no business in a list of places sound goes.
        AudioObjectPropertyAddress ca{kAudioDevicePropertyStreamConfiguration,
                                      kAudioDevicePropertyScopeOutput,
                                      kAudioObjectPropertyElementMain};
        UInt32 configSize = 0;
        if (AudioObjectGetPropertyDataSize(device, &ca, 0, nullptr, &configSize) != noErr
            || configSize == 0) {
            continue;
        }
        std::vector<std::uint8_t> storage(configSize);
        auto *list = reinterpret_cast<AudioBufferList *>(storage.data());
        if (AudioObjectGetPropertyData(device, &ca, 0, nullptr, &configSize, list) != noErr)
            continue;

        UInt32 channels = 0;
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
            channels += list->mBuffers[i].mNumberChannels;
        if (channels == 0)
            continue;

        AudioDevice entry;
        entry.objectId = static_cast<quint32>(device);
        entry.isDefaultOutput = (device == defaultOutput);

        CFStringRef value = nullptr;
        UInt32 valueSize = sizeof(value);
        AudioObjectPropertyAddress ua = addr(kAudioDevicePropertyDeviceUID);
        if (AudioObjectGetPropertyData(device, &ua, 0, nullptr, &valueSize, &value) == noErr
            && value) {
            entry.uid = QString::fromNSString((__bridge_transfer NSString *)value);
        }

        value = nullptr;
        valueSize = sizeof(value);
        AudioObjectPropertyAddress na = addr(kAudioObjectPropertyName);
        if (AudioObjectGetPropertyData(device, &na, 0, nullptr, &valueSize, &value) == noErr
            && value) {
            entry.name = QString::fromNSString((__bridge_transfer NSString *)value);
        }

        if (!entry.uid.isEmpty() && !entry.name.isEmpty())
            result.append(entry);
    }

    std::sort(result.begin(), result.end(), [](const AudioDevice &a, const AudioDevice &b) {
        if (a.isDefaultOutput != b.isDefaultOutput)
            return a.isDefaultOutput;   // wherever sound is going now, first
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}

} // namespace hyperlight::AudioProcessProbe
