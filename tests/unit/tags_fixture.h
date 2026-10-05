#pragma once
//
// Közös, KITALÁLT tesztkönyvtár a címke-tesztekhez: hat megbeszélés rövid átirattal, három
// témában (Nordvik szállítás, belső költségvetés, MuseumPlus bevezetés). A saját mikrofon-sáv
// („Ádám”) mindegyiken ott van — ő a ritkasági súlyozás „mindenhol jelen lévő” résztvevője.
//
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "tanara/store/MeetingStore.h"

namespace tagsfixture {

inline void writeSegments(const QString& folder, const QStringList& lines)
{
    QJsonArray arr;
    qint64 ms = 0;
    for (const QString& l : lines) {
        arr.append(QJsonObject{ { "startMs", double(ms) }, { "endMs", double(ms + 4000) },
                                { "speaker", "Beszélő 1" }, { "text", l } });
        ms += 5000;
    }
    QFile f(QDir(folder).filePath("transcript.segments.json"));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(arr).toJson());
}

inline QString addMeeting(tanara::MeetingStore& store, const QString& title, const QDate& day,
                          const QStringList& people, const QStringList& lines)
{
    tanara::Meeting m = store.createMeeting(title);
    m.startedAt = QDateTime(day, QTime(10, 0));
    m.durationMs = 30 * 60 * 1000;
    tanara::Track mic;
    mic.id = "mic"; mic.kind = tanara::TrackKind::Mic; mic.file = "track_mic.ogg";
    mic.speakerLabel = "Ádám"; mic.fixedSpeaker = true;
    m.tracks.append(mic);
    for (int i = 0; i < people.size(); ++i)
        m.speakerMap.insert(QStringLiteral("Beszélő %1").arg(i + 2), people.at(i));
    m.hasTranscript = !lines.isEmpty();
    if (!lines.isEmpty()) writeSegments(m.folder, lines);
    store.saveMeeting(m);
    return m.id;
}

struct Library {
    QString nordvik1, nordvik2, budget1, budget2, museum1, museum2;
};

inline Library buildLibrary(tanara::MeetingStore& store)
{
    Library l;
    l.nordvik1 = addMeeting(store, "Nordvik ütemterv egyeztetés", QDate(2026, 9, 1),
        { "Kovács Anna", "Fehér Bence" },
        { "Jó reggelt, akkor nézzük a Nordvik szállítási ütemtervet.",
          "A Nordvik raktárban a raklapok átrakása csúszik egy hetet.",
          "A szállítmányozó szerint a kamionok kedden indulnak a raktárból.",
          "Az ütemtervet frissítem, és elküldöm a Nordviknak." });
    l.nordvik2 = addMeeting(store, "Nordvik szállítási határidők", QDate(2026, 9, 8),
        { "Kovács Anna" },
        { "A Nordvik raktár készlete alacsony, a raklapok hiányoznak.",
          "A szállítmányozó új határidőt kért a kamionokra.",
          "Nordvik oldalon Kovács Anna egyezteti az ütemtervet." });
    l.budget1 = addMeeting(store, "Belső tervezés", QDate(2026, 9, 2),
        { "Szabó Csilla" },
        { "A jövő évi költségvetés tervezetét kell átnéznünk.",
          "A toborzás két új fejlesztővel számol a költségvetésben.",
          "A bérkeret és a toborzás üteme még nyitott." });
    l.budget2 = addMeeting(store, "Költségvetés felülvizsgálat", QDate(2026, 9, 9),
        { "Szabó Csilla" },
        { "A költségvetés második változatában a bérkeret szűkebb.",
          "A toborzást a harmadik negyedévre toljuk.",
          "Szabó Csilla összesíti a költségvetési táblát." });
    l.museum1 = addMeeting(store, "MuseumPlus bevezetés", QDate(2026, 9, 3),
        { "Tóth Dénes" },
        { "A MuseumPlus adatbázis migrációját a RemedIT csapata végzi.",
          "A RemedIT kéri a gyűjteményi rekordok exportját.",
          "A MuseumPlus felületén a leltári mezőket egyeztetjük." });
    l.museum2 = addMeeting(store, "MuseumPlus oktatás", QDate(2026, 9, 10),
        { "Tóth Dénes" },
        { "A MuseumPlus oktatást a Remedi tartja a múzeumban.",
          "A gyűjteményi rekordok importja után a leltári mezők rendben vannak.",
          "A MuseumPlus jogosultságokat még be kell állítani." });
    return l;
}

} // namespace tagsfixture
