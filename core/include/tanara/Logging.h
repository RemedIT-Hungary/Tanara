#pragma once
//
// Tanara — központi, cross-platform logolás (Qt logging framework fölött).
//
// Egyetlen kódút Linuxon és Windowson: minden üzenet egy saját message-handleren
// megy át, ami (1) stderr-re és (2) forgatott fájl-logba ír (~/.tanara/logs/).
//   • Linux + KDE Plasma: a stderr-t a systemd-journald automatikusan elkapja
//     (journalctl --user -t tanara) — nincs libsystemd-függőség.
//   • Windows: a fájl-log az igazság forrása (a GUI-nak nincs konzolja) + a
//     handler OutputDebugStringW-re is tükröz (DebugView/VS).
//
// A warning+critical MINDIG naplózódik (a „main error log"-ra), debug nélkül is;
// a szint csak az info/debug bőbeszédűséget kapcsolja. Titok (API-kulcs) SOHA
// nem kerül logba.
//
#include <QLoggingCategory>
#include <QString>
#include <QStringList>

namespace tanara {

// Kategóriák — kódból: qCInfo(lcAudio) << ... / qCDebug(lcStt) << ...
Q_DECLARE_LOGGING_CATEGORY(lcApp)     // tanara.app   — életciklus, beállítás, diagnosztika
Q_DECLARE_LOGGING_CATEGORY(lcAudio)   // tanara.audio — eszközök, felvétel, mixdown
Q_DECLARE_LOGGING_CATEGORY(lcStt)     // tanara.stt   — átírás
Q_DECLARE_LOGGING_CATEGORY(lcLlm)     // tanara.llm   — összefoglaló
Q_DECLARE_LOGGING_CATEGORY(lcStore)   // tanara.store — meeting-tár, perzisztencia
Q_DECLARE_LOGGING_CATEGORY(lcVoice)   // tanara.voice — voice-ID, lenyomatok

enum class LogLevel { Error = 0, Warning = 1, Info = 2, Debug = 3 };

struct LogOptions {
    LogLevel level     = LogLevel::Info;  // alapértelmezett „normál"
    bool     toFile    = true;
    bool     toStderr  = true;
    QString  fileDir;                      // üres → ~/.tanara/logs
    QString  extraRules;                   // --log-rules passthrough (QLoggingCategory szintaxis)
};

// Szöveg → LogLevel ("error|warning|info|normal|debug|verbose" + rövidítések).
LogLevel parseLogLevel(const QString& s, bool* ok = nullptr);
QString  logLevelName(LogLevel l);

// argv-ből (program-névvel együtt) kiolvassa a log-kapcsolókat. Env: TANARA_LOG_LEVEL
// is figyelembe véve (a kapcsoló felülírja). A felismert kapcsolók: --debug, -d,
// -v/-vv, --log-level[=X], --log-dir[=DIR], --no-log-file, --log-rules[=RULES].
LogOptions parseLogOptions(const QStringList& args);

// Eltávolítja a felismert log-kapcsolókat (és értékeiket) az arg-listából, hogy a
// parancs-parsernek (tanara-cli) tiszta argv maradjon. argv[0]-t megtartja.
QStringList stripLogArgs(const QStringList& args);

// Telepíti az üzenetkezelőt, megnyitja a logfájl(oka)t, beállítja a szűrőszabályokat.
// MIELŐTT a QApplication-t konstruálnád hívd (hogy a korai üzenetek is beessenek).
void initLogging(const LogOptions& opts);

QString  currentLogFilePath();   // a fő logfájl abszolút útja ("" ha toFile=false)
LogLevel currentLogLevel();      // az initLogging-nak átadott szint

class AppController;

// Induló diagnosztika: verzió, platform, feloldott mappák, beállítások (TITKOK
// KIHAGYVA), provider-kiválasztás, látott audio-eszközök, tárban lévő meetingek.
// A részletes dump DEBUG szinten jelenik meg; a fejléc INFO szinten.
void logStartupDiagnostics(const AppController& app);

} // namespace tanara
