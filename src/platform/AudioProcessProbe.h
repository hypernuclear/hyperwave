#pragma once

#include "audio/AudioProcess.h"

namespace hyperlight {

// What the audio system knows about which processes exist and which are making
// noise, independent of whether our tap is allowed to hear any of it.
//
// This started as one function answering a single question -- "is anything
// playing right now?" -- because the tap alone cannot tell permission denial
// from an idle machine: both deliver digital silence, and accusing someone of
// denying a permission they were never prompted for is a bad failure. It turns
// out the same property list also answers "which apps", which is what the
// source picker is built on.
//
// Built on kAudioHardwarePropertyProcessObjectList plus
// kAudioProcessPropertyIsRunningOutput / PID / BundleID. Verified on macOS 26:
// bundle ids come through un-redacted once the tap grant is in place.
namespace AudioProcessProbe {

// True if some process other than us is currently outputting audio.
bool isAnyOtherProcessOutputting();

// Whether the process-object API answered at all. False means the probe itself
// is unavailable, which makes its result meaningless rather than negative.
bool isAvailable();

// Everything the audio system will tell us about. Includes unnamed helpers;
// filter with AudioProcess::isNamed() for anything user-facing.
AudioProcessList listProcesses();

// The object ids for a bundle id, resolved fresh. A single app can own several
// process objects (a browser and its media helper, say), and audio can come
// from any of them, so this returns all of them rather than the first.
QVector<quint32> objectIdsForBundle(const QString &bundleId);

} // namespace AudioProcessProbe

} // namespace hyperlight
