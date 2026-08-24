#pragma once

#include <memory>

#include <QVector>

namespace hyperlight {

// Per-application output level.
//
// This exists because CoreAudio will not answer "is this app making a sound".
// kAudioProcessPropertyIsRunningOutput only says a process holds an open output
// stream, which a 3D editor or a paused player does for hours at a time, so any
// list built on it is wrong in the one way that matters.
//
// The measurement is the answer. One private aggregate device can hold several
// taps at once -- kAudioAggregateDeviceTapListKey takes an array -- and each tap
// arrives as its own buffer in a single IOProc. So N applications cost one
// aggregate, one IO thread and an RMS per buffer, rather than N of everything.
// Verified: with music playing and an editor idle, the editor's own tap read
// -120 dB while the player's read -5.9 dB.
class AudioLevelMeter
{
public:
    AudioLevelMeter();
    ~AudioLevelMeter();

    AudioLevelMeter(const AudioLevelMeter &) = delete;
    AudioLevelMeter &operator=(const AudioLevelMeter &) = delete;

    // The processes to meter, by CoreAudio object id. Rebuilds the tap set only
    // when the membership actually changes -- tearing an aggregate down and
    // building it back up on every poll would be far more expensive than the
    // measurement it exists to take.
    void setProcesses(const QVector<quint32> &objectIds);

    void stop();

    // Peak level seen recently, in dBFS. -120 for silent, unmetered or unknown.
    //
    // "Recently" rather than "now" on purpose: the value decays over a couple of
    // seconds instead of tracking the signal, so a gap between tracks or a quiet
    // passage does not make an app blink out of the list.
    float levelDb(quint32 objectId) const;

    bool isAudible(quint32 objectId) const;

    // Public only because the C IOProc trampoline has to reach it; nothing
    // outside this file has any business with it.
    struct Impl;
private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyperlight
