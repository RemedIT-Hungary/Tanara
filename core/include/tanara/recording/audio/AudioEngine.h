#pragma once
//
// Multi-device capture engine. One ma_device per device; each one's real-time callback
// ONLY copies into its own ring buffer + updates an atomic RMS/peak value. No allocation /
// lock / Qt in the callback.
//
// The methods are virtual: tests drive the RecordingSession with a fake engine (without a real
// audio device); see RecordingSession::setEngineFactory.
#include "tanara/domain/Audio.h"
#include "tanara/recording/audio/RingBuffer.h"

#include <QVector>

#include <atomic>
#include <memory>

namespace tanara::recording {

class AudioEngine {
public:
    AudioEngine();
    virtual ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens one capture ma_device per device (s16, 48000 Hz, per-device channel count). True if at
    // least one device started. Returns true even for partial success: devices that did not start are
    // skipped (count() reflects that). Error/empty input -> false, and it resets cleanly (stop()).
    virtual bool start(const QVector<AudioDeviceInfo>& devices);

    // Stops and frees every device and buffer.
    virtual void stop();

    // Opens ONE MORE device in the already running engine (adding a track during recording).
    // Returns the new track's index, -1 on error. Existing indexes do not change. Callable only from the
    // thread owning the engine (the callbacks and the reader thread may run meanwhile).
    virtual int addDevice(const AudioDeviceInfo& device);

    // Closes a device mid-run (e.g. it was unplugged): capture stops, but the slot and its ring buffer
    // stay (the index is stable, the remaining data can still be read out).
    virtual void closeDevice(int trackIndex);
    virtual bool isOpen(int trackIndex) const;

    // At most this many devices can be opened (the slot array has a fixed size so the reader thread can
    // access it safely without a lock while a new device opens).
    static constexpr int kMaxDevices = 64;

    virtual int count() const;

    // Ring buffer of the trackIndex-th (started) device. For an invalid index it returns a reference to
    // an internal empty buffer (never null), so the caller does not crash.
    virtual RingBuffer& buffer(int trackIndex);

    // The given track's latest RMS (0..~1, s16 normalized). 0 for an invalid index.
    virtual float rms(int trackIndex) const;

    // The given track's latest peak (0..~1). 0 for an invalid index.
    virtual float peak(int trackIndex) const;

    // The largest peak measured since the last call (0..~1), then resets it, for the level meter's peak
    // hold (peak() sees only the last block, short spikes would be missed).
    virtual float takePeak(int trackIndex);

    // Channel count of the given started device (the callback copies with it).
    virtual int channels(int trackIndex) const;

    // The AudioDeviceInfo of the given started device (for the recording organizer).
    virtual AudioDeviceInfo deviceInfo(int trackIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tanara::recording