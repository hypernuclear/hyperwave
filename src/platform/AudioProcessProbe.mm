#include "platform/AudioProcessProbe.h"

#include <unistd.h>
#include <vector>

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

bool isAvailable()
{
    bool ok = false;
    (void)processObjects(&ok);
    return ok;
}

bool isAnyOtherProcessOutputting()
{
    bool ok = false;
    const std::vector<AudioObjectID> objects = processObjects(&ok);
    if (!ok)
        return false;

    const pid_t self = getpid();
    for (AudioObjectID object : objects) {
        pid_t pid = 0;
        if (!readPid(object, pid) || pid == self)
            continue;   // our own aggregate would otherwise count as playback
        if (readIsPlaying(object))
            return true;
    }
    return false;
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
        if (NSRunningApplication *app =
                [NSRunningApplication runningApplicationWithProcessIdentifier:pid]) {
            process.name = QString::fromNSString(app.localizedName);
        }

        result.append(process);
    }
    return result;
}

QVector<quint32> objectIdsForBundle(const QString &bundleId)
{
    QVector<quint32> ids;
    if (bundleId.isEmpty())
        return ids;

    // Deliberately every match, not the first. A browser plays through a media
    // helper with the same bundle prefix as its main process, and which one
    // actually carries the audio is not something to guess at -- tapping all of
    // them costs nothing and gets it right.
    for (const AudioProcess &process : listProcesses()) {
        if (process.bundleId == bundleId
            || process.bundleId.startsWith(bundleId + QLatin1Char('.'))) {
            ids.append(process.objectId);
        }
    }
    return ids;
}

} // namespace hyperlight::AudioProcessProbe
