#include "tanara/watcher/CallMonitor.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

#include <QTimer>

namespace tanara::watcher {

struct CallMonitor::Impl {
    DetectorFactory factory;
    std::function<bool()> gate;
    std::unique_ptr<IMeetingDetector> detector;
    QTimer* timer = nullptr;
    bool    seenActive = false;   // whether we saw an active call since the start
    int     inactivePolls = 0;    // consecutive inactive polls after the active one
    QString appName;
};

CallMonitor::CallMonitor(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}

CallMonitor::~CallMonitor() = default;

void CallMonitor::setDetectorFactory(DetectorFactory factory) { d->factory = std::move(factory); }

void CallMonitor::setPollGate(std::function<bool()> gate) { d->gate = std::move(gate); }

bool CallMonitor::active() const { return bool(d->detector); }

bool CallMonitor::start(const WatcherSettings& s)
{
    stop();
    if (d->factory) {
        d->detector = d->factory(s.detectorId);
    } else {
        registerBuiltinDetectors();
        d->detector.reset(s.detectorId.isEmpty()
            ? MeetingDetectorRegistry::instance().createBest()
            : MeetingDetectorRegistry::instance().create(s.detectorId));
    }
    if (!d->detector) {
        qCDebug(lcApp) << "Hívás-vég figyelés: nincs elérhető detektor ezen a platformon.";
        return false;
    }
    // Our own capture (the recording) must never count as a call.
    d->detector->configure(s.knownCallApps, QStringLiteral("tanara"));
    d->seenActive = false;
    d->inactivePolls = 0;
    d->appName.clear();
    d->timer = new QTimer(this);
    connect(d->timer, &QTimer::timeout, this, &CallMonitor::poll);
    d->timer->start(qMax(1, s.detectorIntervalSec) * 1000);
    poll();
    return true;
}

void CallMonitor::stop()
{
    if (d->timer) { d->timer->stop(); d->timer->deleteLater(); d->timer = nullptr; }
    d->detector.reset();
    d->seenActive = false;
    d->inactivePolls = 0;
}

void CallMonitor::poll()
{
    if (!d->detector || (d->gate && !d->gate()))
        return;
    const MeetingSignal sig = d->detector->poll();
    if (sig.active) {
        d->seenActive = true;
        d->inactivePolls = 0;
        if (!sig.appName.isEmpty()) d->appName = sig.appName;
        return;
    }
    if (!d->seenActive)
        return;   // we have not even seen a call yet (a manual recording without a call): nothing to signal
    // Debounce: 2 consecutive inactive polls (a momentary microphone release must not ask).
    if (++d->inactivePolls >= 2) {
        const QString app = d->appName;
        d->seenActive = false;   // it re-arms if the call becomes active again later
        d->inactivePolls = 0;
        qCInfo(lcApp).noquote() << "Hívás véget ért felvétel közben:" << app;
        emit callEnded(app);
    }
}

} // namespace tanara::watcher
