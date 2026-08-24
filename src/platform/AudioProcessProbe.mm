#include "platform/AudioProcessProbe.h"

#include <unistd.h>
#include <vector>

#import <CoreAudio/AudioHardware.h>

namespace hyperlight::AudioProcessProbe {

namespace {

std::vector<AudioObjectID> processObjects(bool *ok)
{
    if (ok)
        *ok = false;

    AudioObjectPropertyAddress a{kAudioHardwarePropertyProcessObjectList,
                                 kAudioObjectPropertyScopeGlobal,
                                 kAudioObjectPropertyElementMain};
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
        UInt32 pidSize = sizeof(pid);
        AudioObjectPropertyAddress pa{kAudioProcessPropertyPID, kAudioObjectPropertyScopeGlobal,
                                      kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyData(object, &pa, 0, nullptr, &pidSize, &pid) != noErr)
            continue;
        if (pid == self)
            continue;   // our own aggregate would otherwise count as playback

        UInt32 running = 0;
        UInt32 runningSize = sizeof(running);
        AudioObjectPropertyAddress ra{kAudioProcessPropertyIsRunningOutput,
                                      kAudioObjectPropertyScopeGlobal,
                                      kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyData(object, &ra, 0, nullptr, &runningSize, &running) == noErr
            && running) {
            return true;
        }
    }
    return false;
}

} // namespace hyperlight::AudioProcessProbe
