#pragma once

namespace hyperlight {

// An oracle for "is anything on this machine actually playing audio right now",
// independent of whether our tap is allowed to hear it.
//
// This exists because the tap alone cannot tell the two apart: a tap we lack
// permission for delivers digital silence, and so does a machine with nothing
// playing. Accusing the user of having denied a permission they never saw a
// prompt for is a bad enough failure that it is worth a second signal.
//
// Built on kAudioHardwarePropertyProcessObjectList plus
// kAudioProcessPropertyIsRunningOutput. Deliberately reads only PID and the
// running flag, never the bundle ID, since that is the property most likely to
// be redacted without a grant.
namespace AudioProcessProbe {

// True if some process other than us is currently outputting audio.
bool isAnyOtherProcessOutputting();

// Whether the process-object API answered at all. False means the probe itself
// is unavailable, which makes its result meaningless rather than negative.
bool isAvailable();

} // namespace AudioProcessProbe

} // namespace hyperlight
