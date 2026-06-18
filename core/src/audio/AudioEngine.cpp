#include "tanara/audio/AudioEngine.h"

#include "miniaudio.h"

#include <QString>

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
    ma_uint32 channels = 1;
    AudioDeviceInfo info;
    // Saját ma_device a slot mellett (külön vektorban tartjuk a stabil címekért).
};

} // namespace

struct AudioEngine::Impl {
    // A ma_device-okat külön heap-objektumokban tartjuk: a callback a
    // pDevice->pUserData-n keresztül a hozzá tartozó DeviceSlot-ra mutat, így a
    // slotok címe a teljes felvétel alatt stabil kell legyen.
    std::vector<std::unique_ptr<ma_device>> devices;
    std::vector<std::unique_ptr<DeviceSlot>> deviceSlots;

    ma_context context{};
    bool contextReady = false;
    bool started = false;

    // Tartalék üres puffer érvénytelen indexekhez.
    RingBuffer emptyRing{2};

    ~Impl() { teardown(); }

    void teardown() {
        for (auto& dev : devices) {
            if (dev) ma_device_uninit(dev.get());
        }
        devices.clear();
        deviceSlots.clear();
        if (contextReady) {
            ma_context_uninit(&context);
            contextReady = false;
        }
        started = false;
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
    slot->peak.store(static_cast<float>(pk) / 32768.0f, std::memory_order_relaxed);
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

    // Az eszközöket az enumeráció szerinti ma_device_id-vel kell megnyitni; ezért
    // lekérjük a context capture-listáját és név szerint párosítunk. Ha nincs
    // pontos egyezés, a default eszközt nyitjuk (pDeviceID = nullptr).
    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;
    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_context_get_devices(&impl_->context, &playbackInfos, &playbackCount,
                           &captureInfos, &captureCount);

    for (const AudioDeviceInfo& want : devices) {
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

        if (ma_device_init(&impl_->context, &cfg, dev.get()) != MA_SUCCESS) {
            // Ezt az eszközt kihagyjuk, a többivel megyünk tovább.
            continue;
        }
        if (ma_device_start(dev.get()) != MA_SUCCESS) {
            ma_device_uninit(dev.get());
            continue;
        }

        impl_->deviceSlots.push_back(std::move(slot));
        impl_->devices.push_back(std::move(dev));
    }

    if (impl_->deviceSlots.empty()) {
        stop();
        return false;
    }

    impl_->started = true;
    return true;
}

void AudioEngine::stop() {
    impl_->teardown();
}

int AudioEngine::count() const {
    return static_cast<int>(impl_->deviceSlots.size());
}

RingBuffer& AudioEngine::buffer(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= count()) return impl_->emptyRing;
    return *impl_->deviceSlots[static_cast<size_t>(trackIndex)]->ring;
}

float AudioEngine::rms(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= count()) return 0.0f;
    return impl_->deviceSlots[static_cast<size_t>(trackIndex)]->rms.load(std::memory_order_relaxed);
}

float AudioEngine::peak(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= count()) return 0.0f;
    return impl_->deviceSlots[static_cast<size_t>(trackIndex)]->peak.load(std::memory_order_relaxed);
}

int AudioEngine::channels(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= count()) return 0;
    return static_cast<int>(impl_->deviceSlots[static_cast<size_t>(trackIndex)]->channels);
}

AudioDeviceInfo AudioEngine::deviceInfo(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= count()) return AudioDeviceInfo{};
    return impl_->deviceSlots[static_cast<size_t>(trackIndex)]->info;
}

} // namespace tanara
