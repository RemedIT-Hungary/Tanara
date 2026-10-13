#pragma once
// Fake capture engine for the tests (without a real audio device): RecordingSession /
// AppController can be driven with it (RecordingSession::setEngineFactory,
// AppController::setRecordingEngineFactory).
#include "tanara/recording/audio/AudioEngine.h"

#include <QElapsedTimer>
#include <QStringList>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace tanara {

// Fake engine: one ring buffer per device. feed() writes the samples due by the wall clock into
// the buffers of the OPEN, non-silent devices (a 440 Hz sine), as if a real capture were
// running. Devices in `silentNames` never deliver data (like WASAPI loopback in silence).
// A device that was `vanish()`ed is "unplugged": it delivers no more data (the real capture
// thread silently dies then) until the recorder closes its track.
class FakeEngine : public recording::AudioEngine {
public:
    struct Slot {
        std::unique_ptr<recording::RingBuffer> ring;
        AudioDeviceInfo info;
        bool open = true;
        QElapsedTimer since;
        qint64 framesFed = 0;
        std::atomic<bool> vanished{false};   // the drain thread reads it too (rms)
    };
    QStringList silentNames;
    std::array<std::unique_ptr<Slot>, kMaxDevices> devs;
    int n = 0;
    // The engine is owned by RecordingSession and deleted on close; the test's feeder timer sees
    // through this flag whether the raw `engine` pointer is still alive (otherwise it would write
    // into freed memory; under load this failed with SIGABRT).
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    ~FakeEngine() override { *alive = false; }

    bool start(const QVector<AudioDeviceInfo>& devices) override {
        for (const AudioDeviceInfo& d : devices) addDevice(d);
        return n > 0;
    }
    void stop() override { for (int i = 0; i < n; ++i) devs[i]->open = false; }
    int addDevice(const AudioDeviceInfo& d) override {
        auto s = std::make_unique<Slot>();
        s->ring = std::make_unique<recording::RingBuffer>(48000 * 2);
        s->info = d;
        s->since.start();
        devs[n] = std::move(s);
        return n++;
    }
    void closeDevice(int i) override { if (i >= 0 && i < n) { devs[i]->open = false; ++closeCalls; } }
    int closeCalls = 0;
    // Plug back in: the device delivers data again (the new addDevice gets a new slot, this only revives the old one).
    void revive(const QString& name) {
        for (int i = 0; i < n; ++i)
            if (devs[i]->info.name == name) devs[i]->vanished = false;
    }
    void vanish(const QString& name) {
        for (int i = 0; i < n; ++i)
            if (devs[i]->info.name == name) devs[i]->vanished = true;
    }
    bool isOpen(int i) const override { return i >= 0 && i < n && devs[i]->open; }
    int count() const override { return n; }
    recording::RingBuffer& buffer(int i) override { return *devs[i]->ring; }
    float rms(int i) const override {
        return (devs[i]->vanished || silentNames.contains(devs[i]->info.name)) ? 0.0f : 0.3f;
    }
    float peak(int i) const override { return rms(i); }
    float takePeak(int i) override { return rms(i); }
    int channels(int) const override { return 1; }
    AudioDeviceInfo deviceInfo(int i) const override { return devs[i]->info; }

    void feed() {
        for (int i = 0; i < n; ++i) {
            Slot& s = *devs[i];
            if (!s.open || s.vanished) continue;
            if (silentNames.contains(s.info.name)) { s.framesFed = s.since.elapsed() * 48; continue; }
            const qint64 due = s.since.elapsed() * 48 - s.framesFed;
            if (due <= 0) continue;
            std::vector<int16_t> buf(static_cast<size_t>(due));
            for (qint64 k = 0; k < due; ++k)
                buf[size_t(k)] = int16_t(8000 * std::sin(2.0 * M_PI * 440.0 * double(s.framesFed + k) / 48000.0));
            s.ring->write(buf.data(), buf.size());
            s.framesFed += due;
        }
    }
};

} // namespace tanara
