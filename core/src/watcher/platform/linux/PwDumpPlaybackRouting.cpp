// IPlaybackRouting on Linux: PipeWire's `pw-dump` (parsed by detail::parsePwDumpGraph).
#include "tanara/watcher/PlaybackRouting.h"

#include "tanara/foundation/log/Logging.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>

namespace tanara::watcher {

namespace {

class PwDumpPlaybackRouting final : public IPlaybackRouting {
public:
    PwDumpPlaybackRouting() : m_bin(QStandardPaths::findExecutable(QStringLiteral("pw-dump"))) {}
    ~PwDumpPlaybackRouting() override { cancel(); }

    bool available() const override { return !m_bin.isEmpty(); }

    AudioGraphSnapshot query(int timeoutMs) override
    {
        if (m_bin.isEmpty()) return {};
        QProcess proc;
        proc.start(m_bin, QStringList{});
        if (!proc.waitForStarted(500)) return {};
        if (!proc.waitForFinished(timeoutMs)) {
            proc.kill();
            proc.waitForFinished(200);
            return {};
        }
        return detail::parsePwDumpGraph(proc.readAllStandardOutput());
    }

    bool queryAsync(Done done) override
    {
        if (m_proc || m_bin.isEmpty()) return false;
        auto* proc = new QProcess;
        m_proc = proc;
        QObject::connect(proc, &QProcess::finished, proc,
                         [this, proc, done = std::move(done)](int code, QProcess::ExitStatus st) {
            const QByteArray out = proc->readAllStandardOutput();
            proc->deleteLater();
            if (m_proc == proc) m_proc = nullptr;
            if (st != QProcess::NormalExit || code != 0) return;
            QElapsedTimer perfTimer;
            perfTimer.start();
            const AudioGraphSnapshot next = detail::parsePwDumpGraph(out);
            qCDebug(lcPerf).noquote() << QStringLiteral("pw-dump feldolgozás: %1 KB, %2 ms")
                                         .arg(out.size() / 1024).arg(perfTimer.elapsed());
            if (done) done(next);
        });
        QObject::connect(proc, &QProcess::errorOccurred, proc, [this, proc](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;
            proc->deleteLater();
            if (m_proc == proc) m_proc = nullptr;
        });
        proc->start(m_bin, QStringList{});
        return true;
    }

    bool busy() const override { return m_proc != nullptr; }

    void cancel() override
    {
        if (!m_proc) return;
        QProcess* proc = m_proc;
        m_proc = nullptr;
        proc->disconnect();
        proc->kill();
        proc->waitForFinished(300);
        delete proc;
    }

private:
    QString m_bin;
    QProcess* m_proc = nullptr;   // the running asynchronous query (owned)
};

} // namespace

std::unique_ptr<IPlaybackRouting> createPlatformPlaybackRouting()
{
    return std::make_unique<PwDumpPlaybackRouting>();
}

} // namespace tanara::watcher
