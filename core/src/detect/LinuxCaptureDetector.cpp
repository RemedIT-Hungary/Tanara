#include <QtGlobal>
#if defined(Q_OS_LINUX)

#include "tanara/detect/LinuxCaptureDetector.h"
#include "tanara/detect/detail/PwDumpParser.h"

#include <QProcess>
#include <QStandardPaths>

namespace tanara {

LinuxCaptureDetector::LinuxCaptureDetector()
{
    // Built-in default: the most common call apps. The watcher overrides it from the settings.
    m_knownApps = { QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
                    QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
                    QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox") };
}

QString LinuxCaptureDetector::id() const
{
    return QStringLiteral("linux-capture");
}

bool LinuxCaptureDetector::isAvailable() const
{
    // pw-dump on the PATH = PipeWire is available. (pactl fallback: a later round.)
    return !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty();
}

void LinuxCaptureDetector::configure(const QStringList& knownCallApps, const QString& selfBinary)
{
    if (!knownCallApps.isEmpty())
        m_knownApps = knownCallApps;
    if (!selfBinary.trimmed().isEmpty())
        m_selfBinary = selfBinary.trimmed();
}

MeetingSignal LinuxCaptureDetector::poll()
{
    // The JSON output of pw-dump is locale-independent. Synchronous run with a short timeout:
    // poll() is called from the watcher's QTimer, not a hot path.
    QProcess proc;
    proc.start(QStringLiteral("pw-dump"), QStringList{});
    if (!proc.waitForStarted(500))
        return {};                       // pw-dump did not start -> no signal
    if (!proc.waitForFinished(1500)) {
        proc.kill();
        proc.waitForFinished(200);
        return {};
    }
    const QByteArray out = proc.readAllStandardOutput();
    return detail::parsePwDump(out, m_knownApps, m_selfBinary);
}

} // namespace tanara

#endif // Q_OS_LINUX
