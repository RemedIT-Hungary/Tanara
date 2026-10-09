#pragma once
//
// Hamis capture-motor a tesztekhez (valódi hangeszköz nélkül): a RecordingSession /
// AppController ezzel hajtható meg (RecordingSession::setEngineFactory,
// AppController::setRecordingEngineFactory).
//
#include "tanara/audio/AudioEngine.h"

#include <QElapsedTimer>
#include <QStringList>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace tanara {

// Hamis motor: eszközönként egy körpuffer. A „feed()” a fali óra szerint esedékes mintákat
// írja a NYITOTT, nem néma eszközök pufferébe (440 Hz-es szinusz) — mintha valódi capture
// futna. A `silentNames`-beli eszközök sosem adnak adatot (mint a WASAPI loopback csendben).
// A `vanish()`-olt eszköz „kihúzott”: többé nem ad adatot (a valódi capture-szál ilyenkor
// csendben elhal), amíg a felvevő le nem zárja a sávját.
class FakeEngine : public AudioEngine {
public:
    struct Slot {
        std::unique_ptr<RingBuffer> ring;
        AudioDeviceInfo info;
        bool open = true;
        QElapsedTimer since;
        qint64 framesFed = 0;
        std::atomic<bool> vanished{false};   // a drain-szál is olvassa (rms)
    };
    QStringList silentNames;
    std::array<std::unique_ptr<Slot>, kMaxDevices> devs;
    int n = 0;
    // A motort a RecordingSession birtokolja és a lezáráskor törli; a teszt feeder-időzítője
    // ezen a jelzőn át látja, hogy a nyers `engine` mutató még él-e (különben felszabadított
    // memóriába írna — terhelés alatt ez SIGABRT-tal bukott).
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    ~FakeEngine() override { *alive = false; }

    bool start(const QVector<AudioDeviceInfo>& devices) override {
        for (const AudioDeviceInfo& d : devices) addDevice(d);
        return n > 0;
    }
    void stop() override { for (int i = 0; i < n; ++i) devs[i]->open = false; }
    int addDevice(const AudioDeviceInfo& d) override {
        auto s = std::make_unique<Slot>();
        s->ring = std::make_unique<RingBuffer>(48000 * 2);
        s->info = d;
        s->since.start();
        devs[n] = std::move(s);
        return n++;
    }
    void closeDevice(int i) override { if (i >= 0 && i < n) { devs[i]->open = false; ++closeCalls; } }
    int closeCalls = 0;
    // Visszadugás: az eszköz újra ad adatot (az új addDevice-nak új slot jár, ez csak a régit éleszti).
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
    RingBuffer& buffer(int i) override { return *devs[i]->ring; }
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
