#include "tanara/audio/DeviceMonitor.h"
#include "tanara/audio/AudioEngine.h"
#include "tanara/Logging.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>

namespace tanara {

// Szálkezelés: a motor (AudioEngine) teljes életciklusa — megnyitás, a ~30 Hz-es mérés,
// lezárás — a saját „tanara-levelmon” szálon fut, így a fő szál sosem vár eszköz-megnyitásra
// (N eszköznél több száz ms). A fő szál csak parancsot küld (start / stop), és sorban állított
// hívással kapja a szinteket. Minden parancs új generációt kap; a háttérszál a már elavult
// indítást ki sem nyitja (összevonás), a fő szál pedig az elavult generáció szintjeit eldobja.
// A motor ugyanazon a szálon nyílik és záródik (Windowson a miniaudio COM-init/uninit
// ugyanazon a szálon marad párban).
namespace {

struct LevelSample {
    QString name;
    float rms = 0.0f;
    float peak = 0.0f;
};

class LevelWorker : public QObject {
public:
    explicit LevelWorker(std::shared_ptr<std::atomic<quint64>> gen) : m_gen(std::move(gen)) {}
    ~LevelWorker() override { stopEngine(); }

    // A háttérszálon fut.
    void start(quint64 gen, const QVector<AudioDeviceInfo>& devices, DeviceMonitor* monitor) {
        if (m_gen->load() != gen) return;   // közben újabb parancs jött: ki sem nyitjuk
        stopEngine();
        QElapsedTimer t;
        t.start();
        auto engine = std::make_unique<AudioEngine>();
        const bool ok = engine->start(devices);
        const qint64 ms = t.elapsed();
        QStringList names;
        if (ok) {
            m_engine = std::move(engine);
            for (int i = 0; i < m_engine->count(); ++i) names << m_engine->deviceInfo(i).name;
            m_runningGen = gen;
            if (!m_timer) {
                m_timer = new QTimer(this);
                m_timer->setInterval(33);   // ~30 Hz
                QObject::connect(m_timer, &QTimer::timeout, this, [this, monitor] { tick(monitor); });
            }
            m_timer->start();
        }
        qCDebug(lcPerf).noquote() << QStringLiteral("szintmérő indítása (háttérszál): %1 → %2 eszköz, %3 ms")
                                     .arg(devices.size()).arg(names.size()).arg(ms);
        QMetaObject::invokeMethod(monitor, [monitor, gen, ok, names]() {
            monitor->d_started(gen, ok, names);
        }, Qt::QueuedConnection);
    }

    void stopEngine() {
        if (m_timer) m_timer->stop();
        if (m_engine) { m_engine->stop(); m_engine.reset(); }
        m_runningGen = 0;
    }

private:
    void tick(DeviceMonitor* monitor) {
        const int n = m_engine ? m_engine->count() : 0;
        if (n == 0) return;
        QVector<LevelSample> batch;
        batch.reserve(n);
        for (int i = 0; i < n; ++i)
            batch.append({ m_engine->deviceInfo(i).name, m_engine->rms(i), m_engine->takePeak(i) });
        const quint64 gen = m_runningGen;
        QMetaObject::invokeMethod(monitor, [monitor, gen, batch]() {
            if (!monitor->d_isCurrent(gen)) return;   // leállított / lecserélt indítás
            for (const LevelSample& s : batch) {
                emit monitor->level(s.name, s.rms);
                emit monitor->levelPeak(s.name, s.rms, s.peak);
            }
        }, Qt::QueuedConnection);
    }

    std::shared_ptr<std::atomic<quint64>> m_gen;
    std::unique_ptr<AudioEngine> m_engine;
    QTimer* m_timer = nullptr;
    quint64 m_runningGen = 0;
};

} // namespace

struct DeviceMonitor::Impl {
    QThread thread;
    LevelWorker* worker = nullptr;
    std::shared_ptr<std::atomic<quint64>> gen = std::make_shared<std::atomic<quint64>>(0);
    bool wanted = false;          // indítás kérve (és nem bukott el)
    QStringList names;            // a ténylegesen megnyitott eszközök (az indítás után)
};

DeviceMonitor::DeviceMonitor(QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>())
{
    d_->worker = new LevelWorker(d_->gen);
    d_->worker->moveToThread(&d_->thread);
    connect(&d_->thread, &QThread::finished, d_->worker, &QObject::deleteLater);
    d_->thread.setObjectName(QStringLiteral("tanara-levelmon"));
    d_->thread.start();
}

DeviceMonitor::~DeviceMonitor()
{
    stop();
    // A motor a saját szálán záródik (a lebontás határidős, lásd AudioEngine / DetachedJob).
    d_->thread.quit();
    d_->thread.wait();
}

bool DeviceMonitor::active() const { return d_->wanted; }

QStringList DeviceMonitor::deviceNames() const { return d_->names; }

void DeviceMonitor::start(const QVector<AudioDeviceInfo>& devices) {
    stop();
    if (devices.isEmpty()) return;
    const quint64 gen = ++*d_->gen;
    d_->wanted = true;
    LevelWorker* worker = d_->worker;
    QMetaObject::invokeMethod(worker, [worker, gen, devices, this]() {
        worker->start(gen, devices, this);
    }, Qt::QueuedConnection);
}

void DeviceMonitor::stop() {
    ++*d_->gen;
    d_->wanted = false;
    d_->names.clear();
    LevelWorker* worker = d_->worker;
    QMetaObject::invokeMethod(worker, [worker]() { worker->stopEngine(); }, Qt::QueuedConnection);
}

bool DeviceMonitor::d_isCurrent(quint64 gen) const { return gen != 0 && gen == d_->gen->load(); }

void DeviceMonitor::d_started(quint64 gen, bool ok, const QStringList& names) {
    if (!d_isCurrent(gen)) return;
    if (!ok) {   // nem sikerült megnyitni → csendben kilép (mint korábban)
        d_->wanted = false;
        d_->names.clear();
        return;
    }
    d_->names = names;
}

} // namespace tanara
