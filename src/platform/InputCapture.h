#pragma once

#include <memory>

#include "audio/AudioRing.h"

namespace hyperwave {

// Capture from an input device -- a microphone, a line in, an interface.
//
// Separate from the tap because it has to be. A CoreAudio process tap answers
// "what is this machine playing"; there is no version of it that answers "what
// can this machine hear". Those are different devices with different clocks and
// a different permission, so this is a second IO proc writing into a second
// ring, and the DSP sums the two.
//
// It also carries a different failure mode, and one worth being careful about.
// Microphone access is its own TCC grant, and unlike the audio-capture one it
// does prompt -- but a denied grant still gives a running IO proc delivering
// silence, exactly like the tap does. So a machine that has said no looks
// identical to a quiet room, and nothing here treats silence as evidence of
// either.
class InputCapture
{
public:
    InputCapture();
    ~InputCapture();

    InputCapture(const InputCapture &) = delete;
    InputCapture &operator=(const InputCapture &) = delete;

    bool start();
    void stop();
    bool isRunning() const;

    // The rate the input device is actually delivering at, or 0 when stopped.
    //
    // Worth reading rather than assuming: the analysis is configured from the
    // *output* rate, so if an interface is running its input at 44.1k while the
    // output is at 48k, the mixed-in signal lands about a semitone out. Both
    // devices are usually at the same rate and it does not arise, but "usually"
    // is why this is exposed instead of hidden.
    double sampleRate() const;

    MonoRing *ring();

    // Public only because the C IO proc trampoline has to reach it; nothing
    // outside that file has any business with it.
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperwave
