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

    // Listens to the default input, and keeps following it -- AirPods
    // connecting, an interface unplugged -- until stop().
    bool start();
    void stop();
    bool isRunning() const;

    // The rate the input device is actually delivering at, or 0 when stopped.
    double sampleRate() const;

    // The analysis rate. The ring is written at this rate, not the device's:
    // the DSP sums input and output sample for sample, and the two devices
    // need not agree. A MacBook Pro's own mic runs at 96k, and mixed raw into
    // a 48k analysis it landed an octave low. Safe to call at any time.
    void setTargetRate(double rate);

    MonoRing *ring();
    std::uint64_t backlogFrames() const;

    // Public only because the C IO proc trampoline has to reach it; nothing
    // outside that file has any business with it.
    struct Impl;

private:
    bool open();
    void close();
    void reopen();
    void follow(bool on);
    void watchRate();

    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperwave
