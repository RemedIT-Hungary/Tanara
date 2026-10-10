#pragma once
//
// PeopleStore — a személyek globális tára (<metaadat-mappa>/people.json): személyenként EGY
// rekord (név, becenevek, szabad szöveges megjegyzés), meetingek közt újrahasználva a
// beszélő-átnevezéshez és a személyválasztókhoz. A meeting-specifikus nyers→név leképezés a
// Meeting.speakerMap-ben él, nem itt; a hanglenyomatok a voiceprints.json-ban, névhez kötve.
//
// A fájl alakja (2-es verzió):
//   {
//     "version": 2,
//     "people":   [ {"name": "…", "aliases": ["…"], "note": "…"}, … ],   // a névlista
//     "unlisted": [ {"name": "…", "aliases": ["…"], "note": "…"}, … ]    // csak ha van ilyen
//   }
// - A személy azonosítója a NÉV (kisbetű-függetlenül), ahogy a voiceprints.json-ban is.
// - Az ismeretlen (jövőbeli) mezőket a gyökérben és a rekordokban is megőrzi.
// - "unlisted": olyan név adatai, amely NINCS a névlistán — pl. csak hanglenyomata van, vagy a
//   nevét kívülről átírták, és a becenevei / megjegyzése a régi néven maradtak. Ezek nem
//   vesznek el: details() név szerint megtalálja őket, és ha a név (újra) felkerül a listára
//   (add / rename), a rekord visszakerül a "people" tömbbe. Üres ilyen rekordot nem tárolunk.
//
// Egyszeri, automatikus átállás a régi alakról (betöltéskor): a régi people.json egy sima
// névlista volt ({"people": ["A", "B"]}), a becenevek és a megjegyzés pedig egy testvér-
// fájlban, a people-details.json-ban éltek. Betöltéskor a kettő egyesül, az új alak zár alatt,
// atomikusan kerül a lemezre, és a people-details.json CSAK ezután törlődik. Ha az írás nem
// sikerül, mindkét régi fájl érintetlen marad, a tár a memóriában dolgozik tovább, és a
// következő sikeres mentés fejezi be az átállást. A részleteket lásd a .cpp-ben.
//
// Több folyamat is írhatja: minden módosítás zár alatt visszaolvassa a lemez friss állapotát
// (ha változott), arra alkalmazza a változást, és atomikusan ír (lásd store/SharedFile.h).
//
#include "tanara/store/SharedFile.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct PersonDetails {
    QString     name;
    QStringList aliases;
    QString     note;
    bool isEmpty() const { return aliases.isEmpty() && note.isEmpty(); }
};

class PeopleStore {
public:
    explicit PeopleStore(const QString& filePath = QString());  // üres → <metaadat-mappa>/people.json

    // ---- névlista ----
    QStringList names() const;
    // Hozzáadja, ha még nincs (kisbetű-függetlenül). Ha a névhez listán kívüli rekord
    // tartozik, az visszakerül a listára (a becenevekkel, megjegyzéssel együtt).
    void add(const QString& name);
    // Átnevezés: a TELJES rekord az új névre kerül. Ha az új néven már van rekord, a kettő
    // egyesül (becenevek uniója; megjegyzés: a célé, ha üres, a forrásé; ha mindkettő van,
    // egymás alá). oldNameAsAlias: a régi név becenév lesz (ha csak kis-/nagybetűben tér el
    // az újtól, akkor nem). Az új név mindenképp a listára kerül.
    void rename(const QString& oldName, const QString& newName, bool oldNameAsAlias = false);
    // A személy törlése: a teljes rekord (becenevek, megjegyzés) megszűnik.
    void remove(const QString& name);
    // Csak a névlistáról veszi le (pl. egy „új személy” lépés visszavonása): ha vannak
    // becenevei / megjegyzése, azok listán kívüli rekordként megmaradnak.
    void unlist(const QString& name);

    // ---- becenevek, megjegyzés (bármely névre, akkor is, ha nincs a listán) ----
    PersonDetails details(const QString& name) const;   // ismeretlen név → üres (a név kitöltve)
    QStringList aliases(const QString& name) const { return details(name).aliases; }
    // Becenév felvétele. false: üres, megegyezik a névvel, vagy már szerepel (kisbetű-függetlenül).
    bool addAlias(const QString& name, const QString& alias);
    // Becenév törlése. Vissza: a törölt becenév helye a listában (-1 = nem volt ilyen).
    int removeAlias(const QString& name, const QString& alias);
    // Becenév visszatétele az eredeti helyére (visszavonás).
    bool insertAlias(const QString& name, const QString& alias, int index);
    void setNote(const QString& name, const QString& note);

    // ---- tanult sáv-oldal (a megerősített sorokból / kézi sáv-beosztásból; SideAnalysis) ----
    // A rekord "defaultSide" mezője: "local" | "remote"; üres = nincs tanult érték.
    QString defaultSide(const QString& name) const;
    // Üres side → a mező törlődik. true, ha változott.
    bool setDefaultSide(const QString& name, const QString& side);
    // Minden tanult érték: név → oldal.
    QHash<QString, QString> defaultSides() const;

    // A listán kívüli, de adatot hordozó rekordok nevei (lásd fent: "unlisted").
    QStringList unlistedNames() const;

    // Ha a fájl a lemezen megváltozott (másik folyamat írta), újraolvassa. Olvasás előtt hívd.
    void refresh() { reloadIfChanged(); }

    // Igaz, amíg a régi alakról való átállás még nincs a lemezen (az írás nem sikerült):
    // a tár a memóriában dolgozik, a régi fájlok érintetlenek.
    bool migrationPending() const { return m_migrationPending; }

    QString filePath() const { return m_filePath; }
    // A régi testvér-fájl (people-details.json) helye ugyanabban a mappában.
    QString legacyDetailsPath() const;

private:
    struct Entry {
        PersonDetails d;
        QJsonObject   raw;            // a rekord ismeretlen mezői
        bool          listed = true;  // szerepel a névlistán
    };
    // A régi people-details.json sorsa a következő sikeres mentés után.
    enum class LegacyDetails { None, Remove, SetAsideCorrupt, SetAsideLeftover };

    int indexOf(const QString& name) const;
    Entry& ensure(const QString& name);     // ha nincs: listán kívüli rekordként létrehozza
    void sortEntries();
    void load();
    void reloadIfChanged();
    bool persist();
    void migrateIfNeeded();
    void finishLegacyDetails();

    QString        m_filePath;
    QVector<Entry> m_entries;
    QJsonObject    m_rootExtra;                 // a gyökér ismeretlen mezői
    int            m_version = 0;               // a beolvasott fájl verziója (0 = régi alak / nincs)
    FileStamp      m_stamp;                     // a fájl állapota az utolsó betöltéskor / mentéskor
    bool           m_corrupt = false;           // a lemezen lévő fájl értelmezhetetlen (mentéskor félretesszük)
    bool           m_migrationPending = false;  // a lemezen még a régi alak van
    LegacyDetails  m_legacy = LegacyDetails::None;
};

} // namespace tanara
