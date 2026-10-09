#include "tanara/audio/AudioEngine.h"
#include "tanara/audio/DetachedJob.h"
#include "tanara/Logging.h"

#include "miniaudio.h"

#include <QString>

#include <array>
#include <atomic>
#include <chrono>
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

namespace {

// Egy eszköz lebontásának felső korlátja (több eszköznél közös határidő). A normál út
// néhány ms; ennyi után a beragadt eszközt elengedjük (lásd markReleased()).
constexpr std::chrono::milliseconds kDeviceTeardownTimeout{1500};

// Egy ma_device leállítása és felszabadítása.
//
// Miért kell a ma_device_stop() a ma_device_uninit() ELŐTT: a miniaudio 0.11.x
// ma_device_uninit()-je csak „uninitialized”-ra állítja az állapotot, jelez a wakeupEventen,
// majd megvárja (join) az eszköz munkaszálát. A PulseAudio-backend munkaszála viszont nem azon
// az eseményen áll, hanem a pa_mainloop_iterate(block=1) → poll()-ban, és az állapotot csak a
// következő pulse-esemény után nézi meg újra. Élő eszköznél ez ~10 ms múlva jön (adat),
// egy kihúzott eszköznél (a stream DONT_MOVE-val nyílt, a szerver megöli) SOHA → a join örökre
// blokkol (a 2026-10-09-i fagyás: GUI-szál a pthread_join-ban). A ma_device_stop() ezzel
// szemben meghívja a backend onDeviceDataLoopWakeup-ját (pa_mainloop_wakeup), így a hurok
// kilép, a cork a halott streamen azonnal hibával visszatér, az állapot „stopped” lesz, és az
// ezt követő uninit már csak a várakozó munkaszálat ébreszti fel.
void stopAndUninit(ma_device* dev) {
    ma_device_stop(dev);     // eredménye közömbös: halott streamnél a cork hibát ad, a szál mégis megáll
    ma_device_uninit(dev);
}

} // namespace

struct AudioEngine::Impl {
    // A slotok RÖGZÍTETT méretű tömbben élnek, a darabszám atomi: az olvasó (drain) szál
    // zár nélkül éri el őket, miközben a fő szál új eszközt nyit (addDevice). Egy slot előbb
    // teljesen elkészül, és csak utána nő a count. A callback a pDevice->pUserData-n át a
    // saját slotjára mutat, ezért a slotok címe a motor teljes életében stabil.
    std::array<std::unique_ptr<DeviceSlot>, AudioEngine::kMaxDevices> deviceSlots;
    std::array<std::unique_ptr<ma_device>, AudioEngine::kMaxDevices> devices;
    // Egy elengedett (határidőn túl le nem bontott) eszköz slotja: a callbackje még
    // írhat bele, ezért a slot sosem szabadul fel (szándékos szivárgás).
    std::array<bool, AudioEngine::kMaxDevices> slotLeaked{};
    std::atomic<int> count{0};

    // Heapen, hogy egy beragadt eszköz esetén elengedhető legyen (a beragadt szál még
    // hivatkozik rá a miniaudión belül).
    std::unique_ptr<ma_context> context;
    bool contextReady = false;
    bool contextLeaked = false;   // volt elengedett eszköz → a context nem bontható le
    bool started = false;

    // Tartalék üres puffer érvénytelen indexekhez.
    RingBuffer emptyRing{2};

    ~Impl() { teardown(); }

    // Egy eszköz lebontásának elindítása egy háttérszálon. A ma_device a munkáé lesz.
    static DetachedJob launchRelease(std::unique_ptr<ma_device> device) {
        ma_device* dev = device.release();
        return DetachedJob([dev] { stopAndUninit(dev); delete dev; });
    }

    // A határidőig nem végzett lebontás: a slot és a context marad (a beragadt szál és a
    // callback még hivatkozhat rájuk); az alkalmazás megy tovább.
    void markReleased(int i, bool finished) {
        if (finished) return;
        slotLeaked[static_cast<size_t>(i)] = true;
        contextLeaked = true;
        const DeviceSlot* s = deviceSlots[static_cast<size_t>(i)].get();
        qCWarning(lcAudio).noquote()
            << "Hangeszköz lebontása nem fejeződött be" << kDeviceTeardownTimeout.count()
            << "ms alatt (eltűnt eszköz?), a szála elengedve:" << (s ? s->info.name : QString());
    }

    // Minden eszköz leállítása és felszabadítása. Sosem blokkol kDeviceTeardownTimeout-nál
    // tovább: az eszközök párhuzamosan, háttérszálakon bomlanak le.
    void teardown() {
        const int n = count.load();
        std::vector<std::pair<int, DetachedJob>> jobs;
        for (int i = 0; i < n; ++i)
            if (devices[static_cast<size_t>(i)])
                jobs.emplace_back(i, launchRelease(std::move(devices[static_cast<size_t>(i)])));
        const auto deadline = DetachedJob::Clock::now() + kDeviceTeardownTimeout;
        for (auto& [i, job] : jobs) markReleased(i, job.waitUntil(deadline));

        count.store(0);
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<size_t>(i);
            if (slotLeaked[k]) (void)deviceSlots[k].release();   // szándékos: lásd slotLeaked
            else deviceSlots[k].reset();
            slotLeaked[k] = false;
        }
        if (contextReady) {
            if (contextLeaked) {
                qCWarning(lcAudio) << "A miniaudio context elengedve (beragadt eszköz hivatkozik rá).";
                (void)context.release();
            } else {
                ma_context_uninit(context.get());
            }
            context.reset();
            contextReady = false;
        }
        contextLeaked = false;
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
        ma_context_get_devices(context.get(), &playbackInfos, &playbackCount,
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

        if (ma_device_init(context.get(), &cfg, dev.get()) != MA_SUCCESS)
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

    impl_->context = std::make_unique<ma_context>();
    if (ma_context_init(nullptr, 0, nullptr, impl_->context.get()) != MA_SUCCESS) {
        impl_->context.reset();
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
    // A lebontás háttérszálon fut, a hívó (GUI) legfeljebb kDeviceTeardownTimeout-ig vár rá;
    // a slot (és a ring) megmarad, a maradék adat kiolvasható.
    if (auto& dev = impl_->devices[static_cast<size_t>(trackIndex)]) {
        const DetachedJob job = Impl::launchRelease(std::move(dev));
        impl_->markReleased(trackIndex, job.waitFor(kDeviceTeardownTimeout));
    }
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
