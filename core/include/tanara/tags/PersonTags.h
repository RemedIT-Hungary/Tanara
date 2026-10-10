#pragma once
//
// Címkék a személyeken — tanult kapcsolat, küszöbök, bizonyíték (UI-független, tiszta adat).
//
// Fogalmak (lásd a vault UX-tervét: „Tanara — UX terv (2026-10-10) Személy-címkék”):
//  - M(t): a t címke megbeszélései; M(p): azok a megbeszélések, ahol p nevesített résztvevő
//    (MeetingLibrary::participantsOf); c(p,t) = |M(p) ∩ M(t)|.
//  - Tanult kapcsolat (nincs tárolva): c(p,t) a megbeszélésekből számolva. A saját személy
//    mindenhol kimarad (minden megbeszélésen ott van, nem ad információt).
//  - Kézi címke: a people.json rekord "tags" tömbje (PeopleStore), a TagService kezeli.
//
// A PersonTagStats megváltoztathatatlan pillanatkép; a PersonTagSnapshot (statisztika + kézi
// címkék + címkenevek) szálak között átadható, így a bizonyíték (evidence) háttérszálon is
// számolható (azonosítás / recheck).
//
#include "tanara/tags/TagTypes.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

// Egy megbeszélés a számoláshoz (a TagService sor-gyorsítótárából).
struct PersonTagRow {
    QString     meetingId;
    QDateTime   startedAt;
    QStringList tagIds;
    QStringList participants;   // nevesített résztvevők
};

// Egy (személy, címke) pár számokkal — lista-sorokhoz és javaslatokhoz.
struct PersonTagStat {
    QString   name;              // a személy (megjelenített írásmód)
    QString   tagId;
    int       shared = 0;        // c(p,t)
    int       tagTotal = 0;      // |M(t)|
    int       personTotal = 0;   // |M(p)|
    bool      manual = false;    // a címke kézzel rajta van a személyen
    bool      rejected = false;  // a (személy, címke) javaslat elutasítva
    QDateTime lastShared;        // a legutóbbi közös megbeszélés
};

// Egy támogató / ellentmondó címke a bizonyítékban.
struct TagEvidenceItem {
    QString tagId;
    QString name;
    int     shared = 0;      // c(p,t)
    int     total = 0;       // |M(t)|
    bool    manual = false;  // kézi címke (különben tanult)
};

// A címke mint azonosítási bizonyíték egy személyre egy megbeszélésen. Súlyt NEM tartalmaz
// (azt a rangsoroló adja); csak valószínűség, sosem döntés.
struct TagEvidence {
    QStringList supportTagIds;       // a megbeszélés címkéi, amelyek a személyt támogatják
    QStringList contradictTagIds;    // ellentmondásnál: a megbeszélés címkéi (egyikhez sem tartozik)
    QVector<TagEvidenceItem> items;  // a támogató, ill. ellentmondó címkék számokkal
    QStringList personTagIds;        // a személy kézi címkéi (a „Más címkéken szokott lenni” szöveghez)
    QString text;                    // „#Nordvik · 11 / 14”, több címkénél „#Nordvik +1 · 11 / 14”
    bool isSupport() const { return !supportTagIds.isEmpty(); }
    bool isContradiction() const { return !contradictTagIds.isEmpty(); }
    bool isEmpty() const { return !isSupport() && !isContradiction(); }
};

namespace persontags {

// Küszöbök (rögzítettek; a termékgazda döntése szerint nem állíthatók).
constexpr int kMinShared = 2;            // ≥ 2 közös megbeszélés
constexpr int kTagRatioPct = 30;         // ≥ 30 % a címke megbeszéléseiből
constexpr int kPersonRatioPct = 50;      // vagy ≥ 50 % a személy megbeszéléseiből (csak 3.2)
constexpr int kMaxSuggestions = 3;

// A személy-név összehasonlító kulcsa (kisbetű-független, szélek nélkül).
QString personKey(const QString& name);

// 3.1 / bizonyíték: c ≥ 2 és c / |M(t)| ≥ 30 %.
bool learnedLink(int shared, int tagTotal);
// 3.2: c ≥ 2 és (c / |M(t)| ≥ 30 % vagy c / |M(p)| ≥ 50 %).
bool suggestableForPerson(int shared, int tagTotal, int personTotal);

} // namespace persontags

// A tanult kapcsolat statisztikája (megváltoztathatatlan; bármely szálról olvasható).
class PersonTagStats {
public:
    // A saját név (selfName) minden számolásból kimarad; a résztvevők nincsenek levágva.
    static PersonTagStats compute(const QVector<PersonTagRow>& rows, const QString& selfName);

    int tagTotal(const QString& tagId) const { return m_tagTotal.value(tagId); }
    int personTotal(const QString& person) const { return m_personTotal.value(persontags::personKey(person)); }
    int shared(const QString& person, const QString& tagId) const;
    QDateTime lastShared(const QString& person, const QString& tagId) const;
    // A tárolt megjelenítési írásmód (a legutóbbi megbeszélésé); ismeretlennél a bemenet.
    QString displayName(const QString& person) const;
    // A címke résztvevői (c ≥ 1), ill. a személy címkéi (c ≥ 1); sorrend nélkül.
    QStringList peopleOf(const QString& tagId) const;
    QStringList tagsOf(const QString& person) const;
    int meetingCount() const { return m_meetings; }

private:
    QHash<QString, int> m_tagTotal;                          // tagId → |M(t)|
    QHash<QString, int> m_personTotal;                       // personKey → |M(p)|
    QHash<QString, QString> m_display;                       // personKey → név
    QHash<QString, QHash<QString, int>> m_shared;            // tagId → personKey → c
    QHash<QString, QHash<QString, QDateTime>> m_last;        // tagId → personKey → utolsó közös
    QHash<QString, QStringList> m_tagsOf;                    // personKey → tagId-k
    int m_meetings = 0;
};

// Minden, ami a bizonyítékhoz kell, egy szálak között átadható csomagban.
struct PersonTagSnapshot {
    std::shared_ptr<const PersonTagStats> stats;   // sosem null (TagService::personTagSnapshot)
    QHash<QString, QStringList> manual;            // personKey → kézi tagId-k
    QHash<QString, QString> tagNames;              // tagId → név
    QString selfKey;                               // a saját személy kulcsa (üres = nincs)

    // A bizonyíték (tiszta függvény). Szabályok:
    //  - támogat: a megbeszélés címkéje kézzel rajta van a személyen, vagy a tanult kapcsolat
    //    eléri a 3.1 küszöböt (kézi címke nélkül is);
    //  - ellentmond (szigorú): a megbeszélésnek van címkéje, a személynek VAN kézi címkéje,
    //    egyik sem a megbeszélésé, és a tanult kapcsolat egyikkel sem éri el a küszöböt;
    //  - különben (és a saját személynél, címke nélküli megbeszélésnél) üres.
    TagEvidence evidence(const QString& personName, const QStringList& meetingTagIds) const;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::PersonTagStat)
Q_DECLARE_METATYPE(tanara::TagEvidence)
