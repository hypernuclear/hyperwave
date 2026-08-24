#pragma once

#include "audio/AudioDevice.h"
#include "audio/AudioProcess.h"

namespace hyperlight {

// What the audio system knows about which processes exist and which are making
// noise, independent of whether our tap is allowed to hear any of it.
//
// Two lists: the processes the audio system knows about, and the devices sound
// can be sent to. The device list is what the source picker is built on; the
// process list survives only to work out which application to ask for track
// info, and even that ignores who is "playing" -- see below.
namespace AudioProcessProbe {

// NOTE: there is deliberately no "is anything playing" function here any more.
//
// There used to be one, built on kAudioProcessPropertyIsRunningOutput, used as
// a second opinion when deciding whether our tap was being denied audio. The
// property does not mean what the name suggests: it reports that a process
// holds an open output IO, not that it is making a sound. Measured on an idle
// machine, a 3D editor and a paused music player both said yes with the room
// silent -- so the permission probe accused a perfectly healthy, granted tap.
//
// CoreAudio exposes no per-process level, so there is no honest replacement.
// AudioController now treats silence as silence.
// Whether the process-object API answered at all. False means the probe itself
// is unavailable, which makes its result meaningless rather than negative.
bool isAvailable();

// Everything the audio system will tell us about. Includes unnamed helpers;
// filter with AudioProcess::isNamed() for anything user-facing.
AudioProcessList listProcesses();
// Every device with output channels, newest state each call.
AudioDeviceList listOutputDevices();


} // namespace AudioProcessProbe

} // namespace hyperlight
