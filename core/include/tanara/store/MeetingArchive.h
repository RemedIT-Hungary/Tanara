#pragma once
//
// Tanara — megbeszélés-archívum (export / import egyetlen fájlban).
//
// Formátum: sima ZIP (titkosítás nélkül), alapnév „<mappanév>.tanara.zip”. Tartalma:
//   tanara-archive.json          — manifeszt (lásd ArchiveManifest; "version": 1)
//   <mappanév>/…                 — a megbeszélés mappájának fájljai, változatlan névvel
// A mappanév pontosan a felvételek mappája alatti név (yyyy-MM-dd_HHmm_…), így a másik
// gépen ugyanazzal a névvel jön létre. Kimarad: recording.lock, *.tmp, transcript-backup-*
// mappák (nagyok, nem kellenek), *.embeddings.bin és profile.json (levezetett adat, magától
// újraépül). Benne marad: hang, meeting.json (benne a címke-azonosítók), mixdown, átirat,
// beszélő-javítások, összefoglaló, hullámforma-cache, processing.json.
//
// A címkekészlet gépenként külön él (<metadataDir>/tags.json), ezért a manifeszt a
// megbeszélés címkéinek NEVÉT is viszi; a behúzást végző réteg (AppController) ezekből
// oldja fel / hozza létre a helyi címkéket.
//
// Az import csak kicsomagol + ellenőriz; a felvételek közé a MeetingStore::adoptMeetingFolder
// teszi (a .import-* ideiglenes mappából áthelyezve, majd azt eltakarítva).
//
// UI-független, szálbiztos (nincs QObject, nincs megosztott állapot) → munkaszálon futtatható.
//
#include "tanara/Types.h"
#include "tanara/tags/TagTypes.h"

#include <QDateTime>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

namespace tanara {

struct ArchiveManifest {
    int       version = 0;
    QString   app;            // az exportáló Tanara verziója
    QDateTime exportedAt;
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
    int       trackCount = 0;
    QVector<Tag> tags;        // a megbeszélés címkéi (azonosító + név), a felrakás sorrendjében
};

class MeetingArchive {
public:
    static constexpr int kFormatVersion = 1;
    using Progress = std::function<void(int percent)>;

    static QString manifestFileName() { return QStringLiteral("tanara-archive.json"); }
    static QString fileSuffix() { return QStringLiteral(".tanara.zip"); }
    // Javasolt fájlnév: „<mappanév>.tanara.zip”.
    static QString suggestedFileName(const Meeting& m);
    // Kimarad-e az archívumból a mappán belüli (perjeles) relatív út.
    static bool isExcluded(const QString& relativePath);

    // A megbeszélés mappájának becsomagolása zipPath-ba (előbb „.part” fájlba, a végén
    // átnevezve; a meglévő célfájlt felülírja). tags: a megbeszélés címkéi a manifeszthez.
    // cancel: ha igazra vált, a félkész fájl törlődik és false + „Megszakítva.” jön vissza.
    static bool exportMeeting(const Meeting& m, const QString& zipPath, QString* error,
                              Progress progress = {}, const QVector<Tag>& tags = {},
                              const std::atomic<bool>* cancel = nullptr);

    // Kicsomagolás tempRoot/.import-<uuid>/ alá (tempRoot = a felvételek mappája, hogy a
    // behúzás átnevezéssel menjen). Ellenőrzi: manifeszt van, version == 1, pontosan egy
    // felső szintű mappa, abban meeting.json vagy track_*.ogg, nincs abszolút / „..” út,
    // nincs titkosított bejegyzés. Visszaadja a kicsomagolt megbeszélés-mappa útját; hibánál
    // üres + *error, és az ideiglenes mappa törlődik.
    static QString importArchive(const QString& zipPath, const QString& tempRoot, QString* error,
                                 Progress progress = {}, ArchiveManifest* manifest = nullptr);

    // Az ideiglenes kicsomagoló mappák előtagja (a felvételek mappája alatt rejtett mappa).
    static QString tempDirPrefix() { return QStringLiteral(".import-"); }
};

} // namespace tanara
