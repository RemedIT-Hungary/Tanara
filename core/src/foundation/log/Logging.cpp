#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/Paths.h"


#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSysInfo>
#include <QtGlobal>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace tanara {

Q_LOGGING_CATEGORY(lcApp,   "tanara.app")
Q_LOGGING_CATEGORY(lcAudio, "tanara.audio")
Q_LOGGING_CATEGORY(lcStt,   "tanara.stt")
Q_LOGGING_CATEGORY(lcLlm,   "tanara.llm")
Q_LOGGING_CATEGORY(lcStore, "tanara.store")
Q_LOGGING_CATEGORY(lcVoice, "tanara.voice")
Q_LOGGING_CATEGORY(lcPerf,  "tanara.perf")

namespace {

constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;   // 5 MB -> rotation

QMutex      g_mutex;
LogOptions  g_opts;
QFile*      g_logFile = nullptr;   // the full log (filtered by level)
QFile*      g_errFile = nullptr;   // warning+ only (the "main error log")
bool        g_installed = false;

QString defaultLogDir()
{
    // <metadata folder>/logs: ~/.tanara or TANARA_HOME (tanara/Paths.h).
    return paths::metadataFile(QStringLiteral("logs"));
}

const char* levelTag(QtMsgType t)
{
    switch (t) {
        case QtDebugMsg:    return "DBG";
        case QtInfoMsg:     return "INF";
        case QtWarningMsg:  return "WRN";
        case QtCriticalMsg: return "ERR";
        case QtFatalMsg:    return "FTL";
    }
    return "???";
}

QString formatLine(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const QString ts  = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString cat = QString::fromUtf8(ctx.category ? ctx.category : "default");
    QString line = QStringLiteral("%1 [%2] %3: %4").arg(ts, QString::fromLatin1(levelTag(type)), cat, msg);
    // Source location only at debug level (otherwise noisy and code-path-dependent).
    if (g_opts.level == LogLevel::Debug && ctx.file && *ctx.file)
        line += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);
    return line;
}

void writeTo(QFile* f, const QByteArray& bytes)
{
    if (!f || !f->isOpen()) return;
    f->write(bytes);
    f->flush();
}

void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const QString line  = formatLine(type, ctx, msg);
    const QByteArray u8 = line.toUtf8() + '\n';

    QMutexLocker lock(&g_mutex);

    if (g_opts.toStderr) {
        std::fputs(line.toLocal8Bit().constData(), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
    writeTo(g_logFile, u8);

    // The "main error log": warning and worse ALWAYS go here (even without debug).
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        writeTo(g_errFile, u8);

#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>((line + QLatin1Char('\n')).utf16()));
#endif

    if (type == QtFatalMsg) {
        if (g_logFile) g_logFile->flush();
        abort();
    }
}

void rotateIfBig(const QString& path)
{
    QFileInfo fi(path);
    if (fi.exists() && fi.size() > kMaxLogBytes) {
        const QString bak = path + QStringLiteral(".1");
        QFile::remove(bak);
        QFile::rename(path, bak);
    }
}

QFile* openLog(const QString& path)
{
    rotateIfBig(path);
    auto* f = new QFile(path);
    if (!f->open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
        delete f;
        return nullptr;
    }
    return f;
}

} // namespace

LogLevel parseLogLevel(const QString& s, bool* ok)
{
    const QString v = s.trimmed().toLower();
    if (ok) *ok = true;
    if (v == QLatin1String("error")   || v == QLatin1String("err")  || v == QLatin1String("e")) return LogLevel::Error;
    if (v == QLatin1String("warning") || v == QLatin1String("warn") || v == QLatin1String("w")) return LogLevel::Warning;
    if (v == QLatin1String("info")    || v == QLatin1String("normal") || v == QLatin1String("i")) return LogLevel::Info;
    if (v == QLatin1String("debug")   || v == QLatin1String("verbose") || v == QLatin1String("d")) return LogLevel::Debug;
    if (ok) *ok = false;
    return LogLevel::Info;
}

QString logLevelName(LogLevel l)
{
    switch (l) {
        case LogLevel::Error:   return QStringLiteral("error");
        case LogLevel::Warning: return QStringLiteral("warning");
        case LogLevel::Info:    return QStringLiteral("info");
        case LogLevel::Debug:   return QStringLiteral("debug");
    }
    return QStringLiteral("info");
}

LogOptions parseLogOptions(const QStringList& args)
{
    LogOptions o;

    const QByteArray env = qgetenv("TANARA_LOG_LEVEL");
    if (!env.isEmpty()) {
        bool ok = false;
        const LogLevel l = parseLogLevel(QString::fromLocal8Bit(env), &ok);
        if (ok) o.level = l;
    }

    for (int i = 1; i < args.size(); ++i) {   // [0] = program name
        const QString a = args.at(i);
        if (a == QLatin1String("--debug") || a == QLatin1String("-d")) {
            o.level = LogLevel::Debug;
        } else if (a == QLatin1String("-v")) {
            if (int(o.level) < int(LogLevel::Info)) o.level = LogLevel::Info;
        } else if (a == QLatin1String("-vv") || a == QLatin1String("-vvv")) {
            o.level = LogLevel::Debug;
        } else if (a == QLatin1String("--log-level") && i + 1 < args.size()) {
            o.level = parseLogLevel(args.at(++i));
        } else if (a.startsWith(QLatin1String("--log-level="))) {
            o.level = parseLogLevel(a.mid(12));
        } else if (a == QLatin1String("--log-dir") && i + 1 < args.size()) {
            o.fileDir = args.at(++i);
        } else if (a.startsWith(QLatin1String("--log-dir="))) {
            o.fileDir = a.mid(10);
        } else if (a == QLatin1String("--no-log-file")) {
            o.toFile = false;
        } else if (a == QLatin1String("--log-rules") && i + 1 < args.size()) {
            o.extraRules = args.at(++i);
        } else if (a.startsWith(QLatin1String("--log-rules="))) {
            o.extraRules = a.mid(12);
        }
    }
    return o;
}

QStringList stripLogArgs(const QStringList& args)
{
    QStringList out;
    if (!args.isEmpty()) out << args.first();   // the program name stays
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--debug") || a == QLatin1String("-d") ||
            a == QLatin1String("-v") || a == QLatin1String("-vv") || a == QLatin1String("-vvv") ||
            a == QLatin1String("--no-log-file") ||
            a.startsWith(QLatin1String("--log-level=")) ||
            a.startsWith(QLatin1String("--log-dir=")) ||
            a.startsWith(QLatin1String("--log-rules="))) {
            continue;
        }
        if (a == QLatin1String("--log-level") || a == QLatin1String("--log-dir") ||
            a == QLatin1String("--log-rules")) {
            ++i;   // we skip its value too
            continue;
        }
        out << a;
    }
    return out;
}

void initLogging(const LogOptions& opts)
{
    g_opts = opts;

    // Filter rules. Warning+critical stay ON by default (never disabled), so "an error log even without
    // debug". The level only toggles the info/debug noise.
    const bool info  = int(opts.level) >= int(LogLevel::Info);
    const bool debug = int(opts.level) >= int(LogLevel::Debug);

    QString rules;
    rules += QStringLiteral("qt.multimedia.ffmpeg.info=false\n");   // mute the ffmpeg info banner
    rules += QStringLiteral("tanara.*.info=%1\n").arg(info  ? QStringLiteral("true") : QStringLiteral("false"));
    rules += QStringLiteral("tanara.*.debug=%1\n").arg(debug ? QStringLiteral("true") : QStringLiteral("false"));
    rules += QStringLiteral("default.debug=%1\n").arg(debug ? QStringLiteral("true") : QStringLiteral("false"));
    if (!opts.extraRules.isEmpty())
        rules += opts.extraRules + QLatin1Char('\n');
    QLoggingCategory::setFilterRules(rules);

    QMutexLocker lock(&g_mutex);

    // Closing earlier files (for an idempotent repeated call).
    if (g_logFile) { g_logFile->close(); delete g_logFile; g_logFile = nullptr; }
    if (g_errFile) { g_errFile->close(); delete g_errFile; g_errFile = nullptr; }

    if (opts.toFile) {
        const QString dir = opts.fileDir.isEmpty() ? defaultLogDir() : opts.fileDir;
        QDir().mkpath(dir);
        g_logFile = openLog(QDir(dir).filePath(QStringLiteral("tanara.log")));
        g_errFile = openLog(QDir(dir).filePath(QStringLiteral("tanara-error.log")));
    }

    if (!g_installed) {
        qInstallMessageHandler(messageHandler);
        g_installed = true;
    }
}

QString currentLogFilePath()
{
    QMutexLocker lock(&g_mutex);
    return (g_logFile && g_logFile->isOpen()) ? QFileInfo(*g_logFile).absoluteFilePath() : QString();
}

LogLevel currentLogLevel()
{
    return g_opts.level;
}

} // namespace tanara
