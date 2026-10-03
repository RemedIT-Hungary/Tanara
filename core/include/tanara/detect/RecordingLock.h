#pragma once
//
// RecordingLock — „folyik-e épp felvétel" jelző fájl (~/.tanara/recording.lock).
// A felvevő (tanara --record) felveszi induláskor, elengedi leálláskor; a figyelő
// olvassa, hogy ne ajánljon fel rögzítést, ha már megy egy. Headless (core).
//
// A lock a felvevő PID-jét + a meeting-mappát tárolja (JSON). Elavult (crash utáni)
// lock felismerése a PID életének ellenőrzésével (POSIX kill(pid,0) / Win OpenProcess):
// ha a PID már nem él, a lock inaktívnak számít.
//
#include <QString>

namespace tanara {

// ---- példány-hatókör (sandbox-elszigetelés) ------------------------------------------
// TANARA_HOME mellett egy „másik Tanara” fut (teszt / sandbox): a saját metaadat-mappájával,
// saját lock-fájlokkal ÉS saját felvevő-singleton névvel. Így egy sandbox-példány soha nem
// kap kérést a felhasználó valódi felvevőjétől, nem továbbít neki, és nem blokkolja azt.
//
// A hatókör-utótag: üres, ha nincs TANARA_HOME (a valódi példány neve változatlan);
// különben "-<a feloldott mappa SHA-1-ének első 12 hex jegye>". Lokális socket / IPC nevekhez.
QString instanceScopeSuffix();
// A lock-fájlok a (feloldott) metaadat-mappában élnek → TANARA_HOME mellett a sandboxban.
QString recordingLockPath(const QString& configuredMetadataDir = QString());
QString watcherLockPath();

class RecordingLock {
public:
    explicit RecordingLock(const QString& lockPath);

    // Felvétel indításakor: kiírja a saját PID-et + a meeting-mappát. true, ha sikerült.
    bool acquire(const QString& meetingFolder);
    // Felvétel végén: törli a lockot (csak ha a miénk — idegen PID lockját nem bántjuk).
    void release();

    struct Info {
        bool    active = false;      // van ÉLŐ felvevő (a PID létezik)
        qint64  pid = 0;
        QString meetingFolder;
        QString startedAt;           // ISO-8601
    };
    // A figyelő ezt hívja: fut-e élő felvétel? Elavult (halott PID) lock → {active:false}.
    static Info read(const QString& lockPath);

private:
    QString m_path;
    bool    m_owned = false;         // mi vettük-e fel (a release csak ekkor töröl)
};

} // namespace tanara
