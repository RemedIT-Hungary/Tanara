#pragma once
//
// SharedFile — több FOLYAMAT (elemző, felvevő, figyelő) által írt, egészben újraírt
// JSON-fájlok (people.json, voiceprints.json) közös segédei:
//  - FileStamp: a fájl állapota (módosítási idő + méret) — olcsó „változott-e a lemezen,
//    mióta betöltöttem?” ellenőrzéshez;
//  - SharedFileLock: folyamatközi zár a fájl MELLETT (<fájl>.lock, QLockFile) a
//    „visszaolvas → módosít → kiír” lépés idejére, hogy két folyamat ne írja felül egymás
//    változását;
//  - setAsideCorrupt: az értelmezhetetlen fájl félretétele felülírás előtt;
//  - replaceFile: kész fájl a régi helyére úgy, hogy a régi hibánál megmarad (lekeverés).
//
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QString>

#include <filesystem>
#include <system_error>

namespace tanara {

struct FileStamp {
    qint64 mtimeMs = -1;   // -1: a fájl nem létezik
    qint64 size    = -1;

    static FileStamp of(const QString& path)
    {
        FileStamp s;
        const QFileInfo fi(path);
        if (fi.exists()) {
            s.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
            s.size    = fi.size();
        }
        return s;
    }
    bool operator==(const FileStamp& o) const { return mtimeMs == o.mtimeMs && size == o.size; }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

class SharedFileLock {
public:
    // Vár a zárra (legfeljebb waitMs-ig). Ha nem kapja meg (pl. beragadt másik folyamat), a
    // hívó ettől még dolgozhat — locked() jelzi; az összeomlott folyamat zárját a QLockFile
    // (halott PID / elavulási idő alapján) magától feloldja.
    explicit SharedFileLock(const QString& filePath, int waitMs = 5000)
        : m_lock(filePath + QStringLiteral(".lock"))
    {
        m_lock.setStaleLockTime(30000);
        m_locked = m_lock.tryLock(waitMs);
    }
    bool locked() const { return m_locked; }

private:
    QLockFile m_lock;
    bool      m_locked = false;
};

// Az értelmezhetetlen (nem üres) fájl átnevezése „<fájl>.corrupt-<időbélyeg>” névre, hogy a
// következő mentés ne semmisítse meg. true, ha nincs mit félretenni, vagy sikerült.
inline bool setAsideCorrupt(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || fi.size() == 0)
        return true;
    const QString aside = path + QStringLiteral(".corrupt-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QFile::rename(path, aside);
}

// A `to` fájl lecserélése a kész `from` fájlra úgy, hogy a régi `to` SOSEM vész el siker
// nélkül (a „töröld, aztán nevezd át” sorrend hibánál a régit is elveszíti). Elsőként
// atomikus rá-átnevezés; ha az nem megy (pl. a fájlrendszer nem engedi a felülírást), a
// régit félretesszük, és csak az új helyre kerülése után töröljük — hibánál visszatesszük.
// false esetén a `to` a hívás előtti állapotában van, a `from` érintetlen.
inline bool replaceFile(const QString& from, const QString& to)
{
    if (!QFileInfo::exists(from))
        return false;
    std::error_code ec;
    std::filesystem::rename(QFileInfo(from).filesystemAbsoluteFilePath(),
                            QFileInfo(to).filesystemAbsoluteFilePath(), ec);
    if (!ec)
        return true;
    const bool hadOld = QFileInfo::exists(to);
    const QString aside = to + QStringLiteral(".old");
    if (hadOld) {
        QFile::remove(aside);
        if (!QFile::rename(to, aside))
            return false;
    }
    if (QFile::rename(from, to)) {
        if (hadOld) QFile::remove(aside);
        return true;
    }
    if (hadOld) QFile::rename(aside, to);
    return false;
}

} // namespace tanara
