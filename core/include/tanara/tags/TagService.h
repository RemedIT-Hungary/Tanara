#pragma once
//
// TagService — a címkekészlet és a meetingenkénti címkék kezelése (UI-független).
//
// Tárolás:
//  - a készlet: <metadataDir>/tags.json
//      { "version": 1,
//        "tags":     [ {"id", "name", "createdAt", "lastUsedAt"?, "aliases"?: [...]}, … ],
//        "rejected": [ {"meetingId", "tagId"?, "name"?}, … ] }
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
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QObject>
#include <QVector>

#include <memory>

namespace tanara {

class MeetingStore;
class MeetingProfiles;

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

    // ---- levezetett ----
    TagProfile profile(const QString& id) const;
    QVector<QPair<QString, int>> cooccurring(const QString& id) const;   // gyakoriság szerint
    // A profil egysoros szöveges kivonata (az LLM-javaslat jelöltlistájához).
    QString profileLine(const QString& id) const;

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

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
