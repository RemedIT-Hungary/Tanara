#pragma once
//
// VoiceModelDownloader — a beszélő-modellek (ONNX) letöltése a VoiceModelSpec::downloadUrl-ről
// a <metaDir>/models/<fileName> helyre. A CLI (`voice-models fetch`) és a GUI (Beállítások ›
// Beszélők, Első lépések) közös letöltője.
//
//  - aszinkron (QNetworkAccessManager a hívó szálán, nincs saját eseményhurok): a GUI-szálat nem
//    blokkolja; egyszerre több modell is töltődhet (id szerint);
//  - QSaveFile: a célfájl csak sikeres, méret-ellenőrzött letöltés után jelenik meg (hiba,
//    megszakítás vagy méret-eltérés esetén nem marad félkész fájl);
//  - meglévő fájlt NEM ír felül, csak kifejezett `overwrite` mellett (CLI: --force);
//  - hálózat nélkül (QNetworkInformation: Disconnected) el sem indul: Failure::Offline.
//
// Teszt-varrat (hálózat nélkül): a spec downloadUrl-je lehet file:// URL (a QNetworkAccessManager
// helyi fájlt olvas), a NAM injektálható, és az elérhetőség felülírható (setReachableOverride).
//
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

class QNetworkAccessManager;

namespace tanara {

class VoiceModelDownloader : public QObject {
    Q_OBJECT
public:
    // Miért nem sikerült (a felület ebből ír emberi szöveget; az `error` technikai részlet).
    enum class Failure {
        None,
        Offline,        // nincs hálózat (indulás előtt vagy közben: a gép nem éri el a szervert)
        Network,        // egyéb hálózati / HTTP-hiba
        SizeMismatch,   // a letöltött méret eltér a várttól — semmi nem íródott
        Write,          // a célfájl nem írható
        Exists,         // a célfájl már létezik (overwrite nélkül)
        Cancelled,      // cancel()
        NoUrl,          // a leírónak nincs letöltési címe
    };
    Q_ENUM(Failure)

    // A meglévő modellfájl ellenőrzése (Beállítások › Beszélők „ellenőrzés” állapota). Most a
    // fájlméret egyezése a leíró sizeBytes-ával; később hash is.
    enum class VerifyResult {
        Ok,             // a méret egyezik
        Missing,        // nincs ilyen fájl
        SizeMismatch,   // a méret eltér (sérült / félbehagyott / más fájl)
        Unverifiable,   // a leíró nem közöl méretet — nincs mihez mérni (használható)
    };
    Q_ENUM(VerifyResult)

    explicit VoiceModelDownloader(QObject* parent = nullptr);
    ~VoiceModelDownloader() override;

    // A használt NAM (nem birtokolja; nullptr → saját példány). Teszt-varrat.
    void setNetworkAccessManager(QNetworkAccessManager* nam);

    // A letöltési cél: <metaDir>/models/<fileName> (a VoiceModelRegistry első feloldási helye).
    static QString targetPath(const VoiceModelSpec& spec, const QString& metaDir);

    // Letöltés indítása a `target` fájlba. true: elindult (a végén finished jön). false: nem
    // indult (NoUrl / Exists / Write / Offline) — ilyenkor a finished(id, false, …) jel még a
    // hívás alatt kimegy, és lastFailure(id) megmondja az okot. Ha ez a modell már töltődik:
    // false, jel nélkül (a futó letöltés megy tovább).
    bool start(const VoiceModelSpec& spec, const QString& target, bool overwrite = false);
    // Futó letöltés megszakítása (a célfájl nem jön létre). finished(id, false, …) jön,
    // lastFailure = Cancelled. Nem futó id-re nem csinál semmit.
    void cancel(const QString& id);

    bool isRunning(const QString& id) const;
    QStringList runningIds() const;
    // Az id utolsó befejezett letöltésének eredménye (None: sikerült vagy még nem volt).
    Failure lastFailure(const QString& id) const;

    // Van-e hálózat (QNetworkInformation; ha a platformon nincs backend: igaz). Teszt-varrat:
    // setReachableOverride(false/true), std::nullopt → a valódi állapot.
    bool networkReachable() const;
    void setReachableOverride(std::optional<bool> reachable);

    // A hálózati hibák közül azok, amelyek „nincs net” jellegűek (névfeloldás, hálózat le).
    static bool isOfflineError(int networkReplyError);

    // ---- ellenőrzés és törlés (G4, B1) ----
    // Szinkron ellenőrzés (CLI, tesztek). detail: technikai sor (angol), lehet üres.
    static VerifyResult verifyFile(const VoiceModelSpec& spec, const QString& path, QString* detail = nullptr);
    // Aszinkron ellenőrzés a felületnek: verifyStarted(id) még a hívásban, verified(id, …) a
    // következő eseményhurok-körben (addig isVerifying(id) igaz — az „ellenőrzés” állapot).
    // false: már ellenőrződik vagy épp töltődik (jel nélkül).
    bool verify(const VoiceModelSpec& spec, const QString& path);
    bool isVerifying(const QString& id) const;
    // Az id utolsó befejezett ellenőrzésének eredménye (nullopt: még nem volt).
    std::optional<VerifyResult> lastVerify(const QString& id) const;
    // A LETÖLTÖTT modellfájl törlése: csak <metaDir>/models/<fileName> (targetPath); a csomagban
    // (<appDir>/models) lévőhöz nem nyúl. false (+ *error, angol): nincs letöltött fájl / nem
    // törölhető. A futó letöltést / ellenőrzést a hívó zárja ki (isRunning / isVerifying).
    static bool deleteModelFile(const VoiceModelSpec& spec, const QString& metaDir, QString* error = nullptr);

signals:
    void started(const QString& id);
    // total: a szerver által közölt méret, ennek híján a leíró sizeBytes-a (0 = ismeretlen).
    void progress(const QString& id, qint64 received, qint64 total);
    void finished(const QString& id, bool ok, const QString& error);
    // Ellenőrzés (G4, B1).
    void verifyStarted(const QString& id);
    void verified(const QString& id, tanara::VoiceModelDownloader::VerifyResult result, const QString& detail);

private:
    struct Job;
    void finishJob(const QString& id);
    void fail(const QString& id, Failure why, const QString& error);
    QNetworkAccessManager* nam();

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkAccessManager* m_ownNam = nullptr;
    QHash<QString, Job*> m_jobs;
    QHash<QString, Failure> m_last;
    std::optional<bool> m_reachableOverride;
    QHash<QString, VerifyResult> m_verified;   // G4: az utolsó ellenőrzés id szerint
    QStringList m_verifying;                   // G4: épp ellenőrzött id-k
};

} // namespace tanara
