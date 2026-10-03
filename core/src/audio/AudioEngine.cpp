#include "tanara/audio/AudioEngine.h"

#include "miniaudio.h"

#include <QString>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace tanara {

namespace {

constexpr ma_uint32 kSampleRate = 48000;

// Eszközönkénti, a valós idejű callbackből elért állapot. POD-szerű: a callback
// CSAK a ring-be ír és az atomikat frissíti — semmi allokáció, lock vagy Qt.
struct DeviceSlot {
    std::unique_ptr<RingBuffer> ring;
    std::atomic<float> rms{0.0f};
    std::atomic<float> peak{0.0f};
    std::atomic<float> peakMax{0.0f};   // csúcstartás a takePeak()-hez
    std::atomic<bool> open{false};      // a capture fut (closeDevice után hamis)
    ma_uint32 channels = 1;
    AudioDeviceInfo info;
    // Saját ma_device a slot mellett (külön vektorban tartjuk a stabil címekért).
};

} // namespace

static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput,
                         ma_uint32 frameCount);

struct AudioEngine::Impl {
    // A slotok RÖGZÍTETT méretű tömbben élnek, a darabszám atomi: az olvasó (drain) szál
    // zár nélkül éri el őket, miközben a fő szál új eszközt nyit (addDevice). Egy slot előbb
    // teljesen elkészül, és csak utána nő a count. A callback a pDevice->pUserData-n át a
    // saját slotjára mutat, ezért a slotok címe a motor teljes életében stabil.
    std::array<std::unique_ptr<DeviceSlot>, AudioEngine::kMaxDevices> deviceSlots;
    std::array<std::unique_ptr<ma_device>, AudioEngine::kMaxDevices> devices;
    std::atomic<int> count{0};

    ma_context context{};
    bool contextReady = false;
    bool started = false;

    // Tartalék üres puffer érvénytelen indexekhez.
    RingBuffer emptyRing{2};

    ~Impl() { teardown(); }

    void teardown() {
        const int n = count.load();
        for (int i = 0; i < n; ++i)
            if (devices[i]) ma_device_uninit(devices[i].get());
        count.store(0);
        for (int i = 0; i < n; ++i) { devices[i].reset(); deviceSlots[i].reset(); }
        if (contextReady) {
            ma_context_uninit(&context);
            contextReady = false;
        }
        started = false;
    }

    DeviceSlot* slot(int i) const {
        return (i >= 0 && i < count.load(std::memory_order_acquire)) ? deviceSlots[i].get() : nullptr;
    }

    // Megnyit és elindít egy eszközt; siker esetén az új index, különben -1.
    int open(const AudioDeviceInfo& want) {
        const int idx = count.load();
        if (!contextReady || idx >= AudioEngine::kMaxDevices) return -1;

        // Az eszközöket az enumeráció szerinti ma_device_id-vel kell megnyitni; ezért
        // lekérjük a context listáját és név szerint párosítunk. Ha nincs pontos egyezés,
        // a default eszközt nyitjuk (pDeviceID = nullptr).
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;
        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_context_get_devices(&context, &playbackInfos, &playbackCount,
                               &captureInfos, &captureCount);

        // A loopback (rendszerhang) eszközöket Windowson egy PLAYBACK eszköz
        // ma_device_type_loopback-módú megnyitásával vesszük fel → a playback-
        // listából párosítunk. Minden más (mic, ill. Linux-monitor) capture.
        ma_device_type devType = ma_device_type_capture;
        const ma_device_info* matchInfos = captureInfos;
        ma_uint32 matchCount = captureCount;
#if defined(_WIN32)
        if (want.kind == TrackKind::Loopback) {
            devType = ma_device_type_loopback;
            matchInfos = playbackInfos;
            matchCount = playbackCount;
        }
#endif
        const ma_device_id* matchedId = nullptr;
        if (matchInfos) {
            for (ma_uint32 i = 0; i < matchCount; ++i) {
                if (QString::fromUtf8(matchInfos[i].name) == want.name) {
                    matchedId = &matchInfos[i].id;
                    break;
                }
            }
        }

        auto slot = std::make_unique<DeviceSlot>();
        slot->channels = want.channels > 0 ? static_cast<ma_uint32>(want.channels) : 1;
        slot->info = want;
        // ~1 mp tartalék (48000 * ch). Bőven elég a worker drain-ütemhez.
        const size_t cap = static_cast<size_t>(kSampleRate) * slot->channels;
        slot->ring = std::make_unique<RingBuffer>(cap);

        auto dev = std::make_unique<ma_device>();

        ma_device_config cfg = ma_device_config_init(devType);
        // Loopback esetén is a capture.pDeviceID hordozza a (playback) eszköz id-t.
        cfg.capture.pDeviceID = matchedId;        // nullptr → default eszköz
        cfg.capture.format    = ma_format_s16;
        cfg.capture.channels  = slot->channels;
        cfg.sampleRate        = kSampleRate;
        cfg.dataCallback      = dataCallback;
        cfg.pUserData         = slot.get();        // stabil cím (heap)

        if (ma_device_init(&context, &cfg, dev.get()) != MA_SUCCESS)
            return -1;
        if (ma_device_start(dev.get()) != MA_SUCCESS) {
            ma_device_uninit(dev.get());
            return -1;
        }
        slot->open.store(true);
        deviceSlots[idx] = std::move(slot);
        devices[idx] = std::move(dev);
        count.store(idx + 1, std::memory_order_release);   // csak a kész slot után
        return idx;
    }
};

// --- valós idejű callback: triviális, allokáció/lock/Qt MENTES -----------------
static void dataCallback(ma_device* pDevice, void* /*pOutput*/, const void* pInput,
                         ma_uint32 frameCount) {
    auto* slot = static_cast<DeviceSlot*>(pDevice->pUserData);
    if (!slot || !pInput) return;

    const auto* samples = static_cast<const int16_t*>(pInput);
    const ma_uint32 ch = slot->channels;
    const size_t total = static_cast<size_t>(frameCount) * ch;

    // 1) nyers PCM a körpufferbe (felülcsordulásnál a ring eldobja a maradékot).
    slot->ring->write(samples, total);

    // 2) RMS + peak ezen a blokkon (csak atomi store, nincs allokáció).
    double sumSq = 0.0;
    int16_t pk = 0;
    for (size_t i = 0; i < total; ++i) {
        const int16_t s = samples[i];
        const int a = s < 0 ? -static_cast<int>(s) : static_cast<int>(s);
        if (a > pk) pk = static_cast<int16_t>(a);
        const double f = static_cast<double>(s);
        sumSq += f * f;
    }
    float rms = 0.0f;
    if (total > 0) {
        rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(total)) / 32768.0);
    }
    slot->rms.store(rms, std::memory_order_relaxed);
    const float peak = static_cast<float>(pk) / 32768.0f;
    slot->peak.store(peak, std::memory_order_relaxed);
    // Csúcstartás a szintmérőnek: a takePeak() nullázza (a verseny ártalmatlan, legfeljebb
    // egy blokknyi csúcs marad ki).
    if (peak > slot->peakMax.load(std::memory_order_relaxed))
        slot->peakMax.store(peak, std::memory_order_relaxed);
}

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;

bool AudioEngine::start(const QVector<AudioDeviceInfo>& devices) {
    stop();
    if (devices.isEmpty()) return false;

    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        return false;
    }
    impl_->contextReady = true;

    // A be nem indult eszközöket kihagyjuk, a többivel megyünk tovább.
    for (const AudioDeviceInfo& want : devices)
        impl_->open(want);

    if (impl_->count.load() == 0) {
        stop();
        return false;
    }

    impl_->started = true;
    return true;
}

void AudioEngine::stop() {
    impl_->teardown();
}

int AudioEngine::addDevice(const AudioDeviceInfo& device) {
    if (!impl_->started) return -1;
    return impl_->open(device);
}

void AudioEngine::closeDevice(int trackIndex) {
    DeviceSlot* s = impl_->slot(trackIndex);
    if (!s || !s->open.exchange(false)) return;
    // Az uninit megvárja a callback-szál végét; a slot (és a ring) megmarad.
    if (impl_->devices[trackIndex]) ma_device_uninit(impl_->devices[trackIndex].get());
    impl_->devices[trackIndex].reset();
    s->rms.store(0.0f);
    s->peak.store(0.0f);
    s->peakMax.store(0.0f);
}

bool AudioEngine::isOpen(int trackIndex) const {
    const DeviceSlot* s = impl_->slot(trackIndex);
    return s && s->open.load();
}

int AudioEngine::count() const {
    return impl_->count.load(std::memory_order_acquire);
}

RingBuffer& AudioEngine::buffer(int trackIndex) {
    DeviceSlot* s = impl_->slot(trackIndex);
    return s ? *s->ring : impl_->emptyRing;
}

float AudioEngine::rms(int trackIndex) const {
    const DeviceSlot* s = impl_->slot(trackIndex);
    return s ? s->rms.load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::peak(int trackIndex) const {
    const DeviceSlot* s = impl_->slot(trackIndex);
    return s ? s->peak.load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::takePeak(int trackIndex) {
    DeviceSlot* s = impl_->slot(trackIndex);
    return s ? s->peakMax.exchange(0.0f, std::memory_order_relaxed) : 0.0f;
}

int AudioEngine::channels(int trackIndex) const {
    const DeviceSlot* s = impl_->slot(trackIndex);
    return s ? static_cast<int>(s->channels) : 0;
}

AudioDeviceInfo AudioEngine::deviceInfo(int trackIndex) const {
    const DeviceSlot* s = impl_->slot(trackIndex);
    return s ? s->info : AudioDeviceInfo{};
}

} // namespace tanara
