#pragma once
//
// TagService — a címkekészlet és a meetingenkénti címkék kezelése (UI-független).
//
// Tárolás:
//  - a készlet: <metadataDir>/tags.json
//      { "version": 1,
//        "tags":     [ {"id", "name", "createdAt", "lastUsedAt"?, "aliases"?: [...]}, … ],
//        "rejected": [ {"meetingId", "tagId"?, "name"?}, … ] }
//    A "rejected" személy-szintű elemei: {"person", "tagId", "meetingId"?} — meetingId nélkül
//    tartós (személy, címke) elutasítás, meetingId-vel csak arra a megbeszélésre szól.
//    atomikusan írva, folyamatközi zár alatt (a felvevő-folyamat is írhatja; lásd
//    store/SharedFile.h). Az "aliases" az összevont címkék régi nevei: begépelve a megtartott
//    címkét kínálja a mező.
//  - a hozzárendelés: meeting.json "tags" (Meeting::tagIds), a MeetingStore::saveMeeting-en át.
//
// Visszavonás: minden módosító hívás egy lépés (a csoport — beginGroup/endGroup — együtt egy
// lépés). A lépés az érintett állapot „előtte” pillanatképe (készlet + elutasítások + az
// érintett meetingek címkéi); az undo() ezt állítja vissza. Legfeljebb 50 lépés.
//
// A megbeszélések címkéit (és címét, dátumát, résztvevőit) a store-ból lustán, meetingenként
// gyorsítótárazza; a store jelei frissítik.
//
// Címkék a személyeken (lásd tags/PersonTags.h): a kézi címkék a people.json rekordjaiban
// (PeopleStore::setTags), a címkekészlet közös. A személy-címke műveletek ugyanebben a
// visszavonási veremben vannak (a lépés az érintett személyek címkéit is menti). A címke
// törlése / összevonása a személyekre is átvezetődik (egy lépésben). A tanult kapcsolat
// statisztikája a sor-gyorsítótárból számolódik: az ensurePersonStats() után háttérszálon
// (a régi pillanatkép marad érvényben, amíg az új kész; personStatsChanged), előtte az első
// olvasás szinkron (tesztek, CLI). A saját személy minden szabályból kimarad.
//
#include "tanara/tags/PersonTags.h"
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QObject>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

class MeetingStore;
class MeetingProfiles;
class PeopleStore;

class TagService : public QObject {
    Q_OBJECT
public:
    enum class Sort { LastUsed, Alpha, MostUsed };

    // tagsFile: a tags.json útja (jellemzően <metadataDir>/tags.json).
    TagService(MeetingStore* store, const QString& tagsFile, QObject* parent = nullptr);
    ~TagService() override;

    // A címke-profil jellemző kifejezéseihez (nélküle a topTerms üres).
    void setProfiles(MeetingProfiles* profiles);

    // ---- készlet ----
    QVector<TagUsage> all(Sort sort = Sort::LastUsed) const;
    Tag tag(const QString& id) const;                          // üres id, ha ismeretlen
    Tag byName(const QString& name) const;                     // tagKey-egyezés (régi név is)
    QVector<Tag> similarNames(const QString& name, int limit = 3) const;   // nearDuplicate, a pontos kulcs nélkül
    QVector<TagInputRow> inputRows(const QString& typed, int limit = 5) const;
    // Az alapból kijelölt sor indexe a sorokban (HASONLÓ MÁR VAN → a meglévő; különben az első).
    static int preferredRow(const QVector<TagInputRow>& rows);
    int meetingCount(const QString& tagId) const;
    int totalMeetings() const;

    Tag  create(const QString& name);                          // létező kulcsnál a meglévő
    bool rename(const QString& id, const QString& name);       // false: a kulcs másé / üres név
    void merge(const QString& fromId, const QString& keepId);
    void remove(const QString& id);

    // ---- meetingek ----
    QStringList tagsOf(const QString& meetingId) const;        // a meeting sorrendjében
    Tag  addTag(const QString& meetingId, const QString& nameOrId, TagSource source = TagSource::Manual);
    void removeTag(const QString& meetingId, const QString& id);
    void setTags(const QString& meetingId, const QStringList& ids, TagSource source = TagSource::Manual);
    void bulkAdd(const QStringList& meetingIds, const QString& id);
    void bulkRemove(const QStringList& meetingIds, const QString& id);
    QVector<Tag> recent(int limit = 5) const;
    QStringList meetingsWith(const QString& tagId) const;      // legújabb elöl
    MeetingRef meetingRef(const QString& meetingId) const;     // cím + dátum a gyorsítótárból
    // Minden címkézett meeting címkéi (meetingId → tagsOf) — a javaslat-pillanatképhez.
    QHash<QString, QStringList> taggedMeetings() const;

    // ---- levezetett ----
    TagProfile profile(const QString& id) const;
    QVector<QPair<QString, int>> cooccurring(const QString& id) const;   // gyakoriság szerint
    // A profil egysoros szöveges kivonata (az LLM-javaslat jelöltlistájához).
    QString profileLine(const QString& id) const;

    // ---- személyek: kézi címkék ----
    // A kézi címkék tára (people.json). Nélküle a kézi címke-műveletek hatástalanok, a tanult
    // statisztika működik.
    void setPeopleStore(PeopleStore* people);
    // A saját személy neve (minden hívásnál lekérdezve; üres = nincs).
    void setSelfNameProvider(std::function<QString()> provider);
    QString selfName() const;
    bool isSelf(const QString& name) const;

    QStringList tagsOfPerson(const QString& name) const;       // csak létező címkék, sorrendben
    // false: üres / saját név, vagy nincs PeopleStore. Ismeretlen azonosítók kimaradnak.
    bool setPersonTags(const QString& name, const QStringList& ids);
    Tag  addPersonTag(const QString& name, const QString& nameOrId);   // nem létező név → új címke
    void removePersonTag(const QString& name, const QString& id);
    QStringList peopleWith(const QString& tagId) const;        // kézi; név szerint rendezve

    // ---- személyek: tanult kapcsolat + javaslatok ----
    // Háttér-mód bekapcsolása és a statisztika előre számolása (az alkalmazás indításkor hívja).
    void ensurePersonStats();
    bool personStatsBusy() const;
    // Újraszámolás kérése (pl. a saját név változott); a sor-változásokat magától követi.
    void invalidatePersonStats();
    // Számolás MOST, a hívó szálán (CLI, tesztek; a sorokat is betölti). personStatsChanged.
    void refreshPersonStatsNow();
    // A tanult kapcsolat aktuális pillanatképe (sosem null).
    std::shared_ptr<const PersonTagStats> personStats() const;
    // A bizonyítékhoz kell minden — szálak között átadható.
    PersonTagSnapshot personTagSnapshot() const;
    // A címke személyei (Címkék ablak › Személyek): kézi + tanult ≥ küszöb; kézi előbb, aztán
    // a közös megbeszélések száma, döntetlennél a legutóbbi közös megbeszélés. A saját személy
    // kimarad. limit ≤ 0: mind.
    QVector<PersonTagStat> peopleStats(const QString& tagId, int limit = 50) const;
    // A személy címkéi számokkal: kézi (0 közössel is) + minden c ≥ 1 címke; kézi előbb.
    QVector<PersonTagStat> personTagStats(const QString& name) const;
    // 3.1: „Ráteszem” — tanult ≥ küszöb, nincs rajta kézzel, nincs elutasítva, nincs kizárva.
    QVector<PersonTagStat> suggestPeopleForTag(const QString& tagId, const QStringList& excludeNames = {},
                                               int limit = persontags::kMaxSuggestions) const;
    // 3.2: „Ezeken szokott ott lenni” (c ≥ 2 és (≥ 30 % a címkéé vagy ≥ 50 % a személyé)).
    QVector<PersonTagStat> suggestTagsForPerson(const QString& name, int limit = persontags::kMaxSuggestions) const;
    // 3.3: megbeszélés-címke a résztvevők kézi címkéiből: t, ha ≥ 2 résztvevőn rajta van, vagy
    // egyetlen nem-saját résztvevő van és rajta van; a meetingen lévő és ott elutasított kimarad.
    // source = People, indoklás ReasonKind::PeopleTags (a címkét viselő résztvevők nevei).
    QVector<TagSuggestion> suggestTagsForPeople(const QStringList& names, const QString& meetingId) const;
    // Ugyanez a meeting nevesített résztvevőivel (MeetingLibrary::participantsOf).
    QVector<TagSuggestion> suggestMeetingTagsFromParticipants(const QString& meetingId) const;
    // A címke mint azonosítási bizonyíték (lásd PersonTagSnapshot::evidence).
    TagEvidence tagEvidence(const QString& personName, const QStringList& meetingTagIds) const;

    // (személy, címke) elutasítás; meetingId üres = tartós, különben csak arra a megbeszélésre.
    void rejectPersonTag(const QString& name, const QString& tagId, const QString& meetingId = QString());
    bool isRejectedForPerson(const QString& name, const QString& tagId, const QString& meetingId = QString()) const;

    // Személy-műveletek átvezetése (az AppController globális átnevezése / törlése hívja; a
    // people.json rekordot a PeopleStore viszi): az elutasítások a személlyel mennek
    // (összevonásnál unió), a visszavonási verem személy-pillanatképei követik a nevet.
    void renamePersonRefs(const QString& oldName, const QString& newName);
    void forgetPerson(const QString& name);

    // ---- elutasított javaslatok ----
    void reject(const QString& meetingId, const TagSuggestion& suggestion);
    bool isRejected(const QString& meetingId, const QString& tagIdOrName) const;
    int  rejectedCount() const;
    void clearRejected();

    // ---- visszavonás ----
    QString beginGroup(const QString& label);
    void    endGroup();
    bool    canUndo() const;
    QString undoLabel() const;
    void    undo();

    // A tags.json újraolvasása (ha a lemezen változott) és a meeting-gyorsítótár eldobása.
    void reload();

signals:
    void tagsChanged();                       // a készlet (név, darabszám) változott
    void meetingTagsChanged(QString meetingId);
    void rejectedChanged();
    void undoChanged();
    void personTagsChanged(QString name);     // egy személy kézi címkéi változtak (üres: több is)
    void personStatsChanged();                // a tanult kapcsolat statisztikája frissült

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
