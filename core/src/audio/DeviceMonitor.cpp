#include "tanara/audio/DeviceMonitor.h"
#include "tanara/audio/AudioEngine.h"

#include <QTimer>
#include <memory>

namespace tanara {

struct DeviceMonitor::Impl {
    std::unique_ptr<AudioEngine> engine;
    QTimer* timer = nullptr;
};

DeviceMonitor::DeviceMonitor(QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {}

DeviceMonitor::~DeviceMonitor() { stop(); }

bool DeviceMonitor::active() const { return d_->engine != nullptr; }

void DeviceMonitor::start(const QVector<AudioDeviceInfo>& devices) {
    stop();
    if (devices.isEmpty()) return;

    d_->engine = std::make_unique<AudioEngine>();
    if (!d_->engine->start(devices)) {   // nem sikerült megnyitni → csendben kilép
        d_->engine.reset();
        return;
    }

    d_->timer = new QTimer(this);
    d_->timer->setInterval(50);   // ~20 Hz
    connect(d_->timer, &QTimer::timeout, this, [this] {
        const int n = d_->engine ? d_->engine->count() : 0;
        for (int i = 0; i < n; ++i)
            emit level(d_->engine->deviceInfo(i).name, d_->engine->rms(i));
    });
    d_->timer->start();
}

void DeviceMonitor::stop() {
    if (d_->timer) { d_->timer->stop(); d_->timer->deleteLater(); d_->timer = nullptr; }
    if (d_->engine) { d_->engine->stop(); d_->engine.reset(); }
}

} // namespace tanara
