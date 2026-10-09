//
// Tanara — DetachedJob: a hangeszköz-lebontás biztonsági hálója. Egy soha véget nem érő
// lebontás (mint egy kihúzott eszköz beragadt miniaudio-szála) sem blokkolhatja a hívót a
// határidőn túl; a gyors lebontást a hívó megvárja; a munka erőforrásai a munka végén, a
// háttérszálon szabadulnak fel.
//
#include <QtTest>
#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "tanara/audio/DetachedJob.h"

using namespace tanara;
using namespace std::chrono_literals;

namespace {

// Egy „eszköz”, amelynek a leállítása figyelmen kívül hagyja a kérést: addig blokkol, amíg
// a teszt el nem engedi (a valódi esetben: soha).
struct StuckDevice {
    std::atomic<bool> release{false};
    void stopAndUninit() {
        while (!release.load()) std::this_thread::sleep_for(5ms);
    }
};

bool waitUntilTrue(const std::function<bool()>& pred, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > ms) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

} // namespace

class DetachedJobTest : public QObject {
    Q_OBJECT
private slots:
    void quickJobIsAwaited();
    void stuckJobReturnsWithinTimeout();
    void stuckJobsShareOneDeadline();
};

void DetachedJobTest::quickJobIsAwaited()
{
    auto token = std::make_shared<int>(42);
    std::atomic<bool> ran{false};
    DetachedJob job([token, &ran] { ran = true; });
    QVERIFY(job.waitFor(2000ms));
    QVERIFY(job.finished());
    QVERIFY(ran.load());
    // A munka által birtokolt erőforrás a munka végén felszabadul (a háttérszálon).
    QVERIFY(waitUntilTrue([&token] { return token.use_count() == 1; }, 2000));
}

void DetachedJobTest::stuckJobReturnsWithinTimeout()
{
    auto dev = std::make_shared<StuckDevice>();
    QElapsedTimer t;
    t.start();
    {
        DetachedJob job([dev] { dev->stopAndUninit(); });
        QVERIFY(!job.waitFor(200ms));            // nem várunk a végtelenségig
        QVERIFY(!job.finished());
    }                                            // a job megszűnése sem blokkol
    QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
    QVERIFY(dev.use_count() == 2);               // a beragadt szál még birtokolja

    // Ha a beragadt szál egyszer mégis végez, maga takarít el maga után.
    dev->release = true;
    QVERIFY(waitUntilTrue([&dev] { return dev.use_count() == 1; }, 2000));
}

void DetachedJobTest::stuckJobsShareOneDeadline()
{
    // Több beragadt eszköz együtt is csak egyszer fizeti ki a határidőt (párhuzamos lebontás).
    std::vector<std::shared_ptr<StuckDevice>> devs;
    std::vector<DetachedJob> jobs;
    for (int i = 0; i < 4; ++i) {
        devs.push_back(std::make_shared<StuckDevice>());
        auto d = devs.back();
        jobs.emplace_back([d] { d->stopAndUninit(); });
    }
    QElapsedTimer t;
    t.start();
    const auto deadline = DetachedJob::Clock::now() + 200ms;
    int timedOut = 0;
    for (const DetachedJob& j : jobs)
        if (!j.waitUntil(deadline)) ++timedOut;
    QCOMPARE(timedOut, 4);
    QVERIFY2(t.elapsed() < 600, qPrintable(QString::number(t.elapsed())));
    jobs.clear();
    for (auto& d : devs) d->release = true;
    QVERIFY(waitUntilTrue([&devs] {
        for (auto& d : devs) if (d.use_count() != 1) return false;
        return true;
    }, 2000));
}

QTEST_GUILESS_MAIN(DetachedJobTest)
#include "test_detached_job.moc"
