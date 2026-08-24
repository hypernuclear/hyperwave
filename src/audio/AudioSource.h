#pragma once

#include <memory>

#include <QObject>
#include <QString>

#include "audio/AudioRing.h"
#include "dsp/FrameState.h"

namespace hyperlight {

// The platform seam on the capture side.
//
// Everything above this interface -- the ring, the DSP, the renderer -- is
// shared. Everything below it is per-OS: a CoreAudio process tap on macOS,
// WASAPI loopback on Windows, a PipeWire/PulseAudio monitor source on Linux.
class AudioSource : public QObject
{
    Q_OBJECT

public:
    explicit AudioSource(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioSource() override = default;

    // Returns the implementation for the platform we were built for. Never
    // null: platforms without a real backend get one that reports Unsupported,
    // so callers never need a null check.
    static std::unique_ptr<AudioSource> create(QObject *parent = nullptr);

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    // The rate the IO callback is actually delivering, which is not necessarily
    // the rate anything advertised. Changes when the default output device
    // changes; every change bumps formatEpoch().
    virtual double sampleRate() const = 0;
    virtual std::uint32_t formatEpoch() const = 0;

    virtual PermissionState permissionState() const = 0;

    // The consumer side belongs to the DSP thread. The source owns the storage.
    virtual MonoRing *ring() = 0;

    // Diagnostics for the HUD. Cheap atomics, safe to poll from anywhere.
    virtual std::uint64_t callbackCount() const = 0;
    virtual std::uint64_t overrunCount() const = 0;
    virtual QString statusDetail() const = 0;

Q_SIGNALS:
    void permissionStateChanged(int state);
    void formatChanged(double sampleRate, unsigned epoch);
};

} // namespace hyperlight
