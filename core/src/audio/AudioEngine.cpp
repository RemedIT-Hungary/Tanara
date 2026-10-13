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

// Per-device state reached from the real-time callback. POD-like: the callback ONLY writes
// into the ring and updates the atomics: no allocation, lock or Qt.
struct DeviceSlot {
    std::unique_ptr<RingBuffer> ring;
    std::atomic<float> rms{0.0f};
    std::atomic<float> peak{0.0f};
    std::atomic<float> peakMax{0.0f};   // peak hold for takePeak()
    std::atomic<bool> open{false};      // capture is running (false after closeDevice)
    ma_uint32 channels = 1;
    AudioDeviceInfo info;
    // Its own ma_device next to the slot (kept in a separate vector for stable addresses).
};

} // namespace

static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput,
                         ma_uint32 frameCount);

namespace {

// Upper bound for tearing down one device (a shared deadline for several devices). The normal path
// takes a few ms; after this a stuck device is released (see markReleased()).
constexpr std::chrono::milliseconds kDeviceTeardownTimeout{1500};

// Stopping and freeing one ma_device.
//
// Why ma_device_stop() is needed BEFORE ma_device_uninit(): miniaudio 0.11.x's ma_device_uninit()
// only sets the state to "uninitialized", signals the wakeupEvent, then waits (joins) for the
// device's worker thread. The PulseAudio backend's worker thread does not wait on that event but in
// pa_mainloop_iterate(block=1) -> poll(), and rechecks the state only after the next pulse event. For a
// live device that comes in ~10 ms (data); for an unplugged device (the stream was opened with
// DONT_MOVE, the server kills it) NEVER, so the join blocks forever (the 2026-10-09 freeze: the GUI
// thread in pthread_join). ma_device_stop(), by contrast, calls the backend's onDeviceDataLoopWakeup
// (pa_mainloop_wakeup), so the loop exits, the cork on the dead stream returns an error at once, the
// state becomes "stopped", and the following uninit only wakes the waiting worker thread.
void stopAndUninit(ma_device* dev) {
    ma_device_stop(dev);     // the result does not matter: on a dead stream the cork errors, yet the thread stops
    ma_device_uninit(dev);
}

} // namespace

struct AudioEngine::Impl {
    // The slots live in a FIXED-size array and the count is atomic: the reader (drain) thread reaches
    // them without a lock while the main thread opens a new device (addDevice). A slot is fully built
    // first, and only then does count grow. The callback points to its own slot through
    // pDevice->pUserData, so the slots' addresses are stable for the engine's whole life.
    std::array<std::unique_ptr<DeviceSlot>, AudioEngine::kMaxDevices> deviceSlots;
    std::array<std::unique_ptr<ma_device>, AudioEngine::kMaxDevices> devices;
    // The slot of a released device (not torn down by the deadline): its callback may still write
    // into it, so the slot is never freed (an intentional leak).
    std::array<bool, AudioEngine::kMaxDevices> slotLeaked{};
    std::atomic<int> count{0};

    // On the heap so it can be released if a device gets stuck (the stuck thread still refers to it
    // inside miniaudio).
    std::unique_ptr<ma_context> context;
    bool contextReady = false;
    bool contextLeaked = false;   // a device was released, so the context cannot be torn down
    bool started = false;

    // A spare empty buffer for invalid indexes.
    RingBuffer emptyRing{2};

    ~Impl() { teardown(); }

    // Starting a device's teardown on a background thread. The ma_device becomes the work's.
    static DetachedJob launchRelease(std::unique_ptr<ma_device> device) {
        ma_device* dev = device.release();
        return DetachedJob([dev] { stopAndUninit(dev); delete dev; });
    }

    // A teardown not finished by the deadline: the slot and the context stay (the stuck thread and the
    // callback may still refer to them); the application goes on.
    void markReleased(int i, bool finished) {
        if (finished) return;
        slotLeaked[static_cast<size_t>(i)] = true;
        contextLeaked = true;
        const DeviceSlot* s = deviceSlots[static_cast<size_t>(i)].get();
        qCWarning(lcAudio).noquote()
            << "Hangeszköz lebontása nem fejeződött be" << kDeviceTeardownTimeout.count()
            << "ms alatt (eltűnt eszköz?), a szála elengedve:" << (s ? s->info.name : QString());
    }

    // Stops and frees every device. Never blocks longer than kDeviceTeardownTimeout: the devices are
    // torn down in parallel, on background threads.
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
            if (slotLeaked[k]) (void)deviceSlots[k].release();   // intentional: see slotLeaked
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

    // Opens and starts a device; on success the new index, otherwise -1.
    int open(const AudioDeviceInfo& want) {
        const int idx = count.load();
        if (!contextReady || idx >= AudioEngine::kMaxDevices) return -1;

        // Devices must be opened with the ma_device_id from enumeration; so we fetch the context's list and
        // match by name. If there is no exact match, we open the default device (pDeviceID = nullptr).
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;
        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_context_get_devices(context.get(), &playbackInfos, &playbackCount,
                               &captureInfos, &captureCount);

        // On Windows loopback (system audio) devices are captured by opening a PLAYBACK device in
        // ma_device_type_loopback mode, so we match from the playback list. Everything else (mic, or a Linux
        // monitor) is capture.
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
        // ~1 s of headroom (48000 * ch). Plenty for the worker's drain cadence.
        const size_t cap = static_cast<size_t>(kSampleRate) * slot->channels;
        slot->ring = std::make_unique<RingBuffer>(cap);

        auto dev = std::make_unique<ma_device>();

        ma_device_config cfg = ma_device_config_init(devType);
        // For loopback too, capture.pDeviceID carries the (playback) device's id.
        cfg.capture.pDeviceID = matchedId;        // nullptr = default device
        cfg.capture.format    = ma_format_s16;
        cfg.capture.channels  = slot->channels;
        cfg.sampleRate        = kSampleRate;
        cfg.dataCallback      = dataCallback;
        cfg.pUserData         = slot.get();        // stable address (heap)

        if (ma_device_init(context.get(), &cfg, dev.get()) != MA_SUCCESS)
            return -1;
        if (ma_device_start(dev.get()) != MA_SUCCESS) {
            ma_device_uninit(dev.get());
            return -1;
        }
        slot->open.store(true);
        deviceSlots[idx] = std::move(slot);
        devices[idx] = std::move(dev);
        count.store(idx + 1, std::memory_order_release);   // only after the slot is complete
        return idx;
    }
};

// --- real-time callback: trivial, free of allocation/lock/Qt -----------------
static void dataCallback(ma_device* pDevice, void* /*pOutput*/, const void* pInput,
                         ma_uint32 frameCount) {
    auto* slot = static_cast<DeviceSlot*>(pDevice->pUserData);
    if (!slot || !pInput) return;

    const auto* samples = static_cast<const int16_t*>(pInput);
    const ma_uint32 ch = slot->channels;
    const size_t total = static_cast<size_t>(frameCount) * ch;

    // 1) raw PCM into the ring buffer (on overflow the ring drops the rest).
    slot->ring->write(samples, total);

    // 2) RMS + peak over this block (atomic stores only, no allocation).
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
    // Peak hold for the level meter: takePeak() resets it (the race is harmless; at worst one
    // block's peak is missed).
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

    // Devices that did not start are skipped; we go on with the others.
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
    // The teardown runs on a background thread, the caller (GUI) waits for it at most kDeviceTeardownTimeout;
    // the slot (and the ring) stay, the remaining data can be read out.
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
