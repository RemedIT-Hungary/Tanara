#pragma once
//
// Central cross-platform logging (on top of the Qt logging framework).
//
// One code path on Linux and Windows: every message goes through a custom message handler
// that writes to (1) stderr and (2) a rotated log file (~/.tanara/logs/).
//   * Linux + KDE Plasma: systemd-journald captures stderr automatically
//     (journalctl --user -t tanara); no libsystemd dependency.
//   * Windows: the log file is the source of truth (the GUI has no console); the
//     handler also mirrors to OutputDebugStringW (DebugView/VS).
//
// Warning + critical are ALWAYS logged (to the "main error log"), even without debug;
// the level only toggles info/debug verbosity. Secrets (API keys) are NEVER logged.
#include <QLoggingCategory>
#include <QString>
#include <QStringList>

namespace tanara {

// Categories, from code: qCInfo(lcAudio) << ... / qCDebug(lcStt) << ...
Q_DECLARE_LOGGING_CATEGORY(lcApp)     // tanara.app   - lifecycle, settings, diagnostics
Q_DECLARE_LOGGING_CATEGORY(lcAudio)   // tanara.audio - devices, recording, mixdown
Q_DECLARE_LOGGING_CATEGORY(lcStt)     // tanara.stt   - transcription
Q_DECLARE_LOGGING_CATEGORY(lcLlm)     // tanara.llm   - summary
Q_DECLARE_LOGGING_CATEGORY(lcStore)   // tanara.store - meeting store, persistence
Q_DECLARE_LOGGING_CATEGORY(lcVoice)
Q_DECLARE_LOGGING_CATEGORY(lcPerf)   // tanara.perf: load/rebuild times (debug level)   // tanara.voice: voice ID, voiceprints

enum class LogLevel { Error = 0, Warning = 1, Info = 2, Debug = 3 };

struct LogOptions {
    LogLevel level     = LogLevel::Info;  // default "normal"
    bool     toFile    = true;
    bool     toStderr  = true;
    QString  fileDir;                      // empty = ~/.tanara/logs
    QString  extraRules;                   // --log-rules passthrough (QLoggingCategory szintaxis)
};

// Text to LogLevel ("error|warning|info|normal|debug|verbose" + abbreviations).
LogLevel parseLogLevel(const QString& s, bool* ok = nullptr);
QString  logLevelName(LogLevel l);

// Reads the log switches from argv (including the program name). The TANARA_LOG_LEVEL env
// var is honored too (the switch wins). Recognized switches: --debug, -d,
// -v/-vv, --log-level[=X], --log-dir[=DIR], --no-log-file, --log-rules[=RULES].
LogOptions parseLogOptions(const QStringList& args);

// Removes the recognized log switches (and their values) from the arg list so the command
// parser (tanara-cli) sees a clean argv. Keeps argv[0].
QStringList stripLogArgs(const QStringList& args);

// Installs the message handler, opens the log file(s), sets the filter rules.
// Call BEFORE constructing the QApplication (so early messages are captured too).
void initLogging(const LogOptions& opts);

QString  currentLogFilePath();   // absolute path of the main log file ("" if toFile=false)
LogLevel currentLogLevel();      // the level passed to initLogging

class AppController;

// Startup diagnostics: version, platform, resolved folders, settings (SECRETS OMITTED),
// provider selection, audio devices seen, meetings in the store.
// The detailed dump appears at DEBUG level; the header at INFO.
void logStartupDiagnostics(const AppController& app);

} // namespace tanara

#include <QElapsedTimer>
#include <QString>

namespace tanara {
// Timing scope for the tanara.perf log: the destructor prints the block's duration if it
// reaches the threshold (visible only at debug level). Usage: PerfScope perf("X::y", 20);
class PerfScope {
public:
    PerfScope(const char* label, qint64 thresholdMs = 20) : m_label(label), m_threshold(thresholdMs) { m_timer.start(); }
    ~PerfScope()
    {
        const qint64 ms = m_timer.elapsed();
        if (ms >= m_threshold && lcPerf().isDebugEnabled())
            qCDebug(lcPerf).noquote() << QStringLiteral("%1: %2 ms").arg(QString::fromUtf8(m_label)).arg(ms);
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;
private:
    const char* m_label;
    qint64 m_threshold;
    QElapsedTimer m_timer;
};
} // namespace tanara
