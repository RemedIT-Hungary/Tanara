#pragma once
//
// TagBackend — a címke-nézetmodellek (TagInputModel, MeetingTagsModel, TagsViewModel) és az
// adatforrás közötti VARRAT (gui/qml/CONTRACT-TAGS.md, „Controls”).
//
// Wave 1: csak a kitalált adatos TagDemoBackend létezik (TagDemoBackend.h). Wave 2 egy
// controllerre épülő változatot ad (tanara::TagService + AppController javaslat-jelei), és a
// createControllerTagBackend() gyárfüggvényt köti be — a nézetmodellekhez nem kell nyúlni.
//
// Szándékosan Qt-típusos és core-független: a nézetmodellek, a tesztek mock-jai és a demó
// ugyanazt a felületet látják. Az azonosítók a core-éi (QUuid kapcsos zárójel nélkül); a
// demóban „t-…” / „m-…” alakúak.
//
#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara_qml {

// Egy címke a készletből, használati adatokkal (core: TagUsage).
struct TagItem {
    QString id;
    QString name;
    int meetingCount = 0;
    QDateTime firstUsedAt;
    QDateTime lastUsedAt;
};

// Honnan került a címke a megbeszélésre (core: TagSource).
enum class TagAddSource { Manual, Suggestion, Llm, Bulk };

// Egy indok-sor a „Miért?” panelben (core: SuggestionReason).
struct TagReasonItem {
    QString kind;               // "participant" | "terms" | "title" | "peopleTags"
    QStringList values;
};

// Hivatkozás egy megbeszélésre (core: MeetingRef + a hossz a kezelő listájához).
struct TagMeetingItem {
    QString meetingId;
    QString title;
    QDateTime startedAt;
    qint64 durationMs = 0;
};

// Egy javaslat (core: TagSuggestion).
struct TagSuggestionItem {
    QString tagId;              // üres, ha isNew
    QString name;
    bool isNew = false;         // a nyelvi modell új neve, még nincs a készletben
    QString source;             // "similar" | "cooccur" | "llm" | "people"
    double score = 0.0;
    QVector<TagReasonItem> reasons;
    QVector<TagMeetingItem> similarMeetings;
};

// Egy megbeszélés javaslat-állapota: a legutóbbi eredmény (bármely forrásból) + fut-e számolás.
struct TagSuggestionState {
    bool computing = false;
    QString source;             // "similar" | "cooccur" | "llm" | "people" (az items forrása)
    QString baseTagId;          // cooccur: melyik címke „mellé gyakran”
    QVector<TagSuggestionItem> items;
};

// A kezelő jobb oldala (core: TagProfile).
struct TagProfileItem {
    QString tagId;
    int meetingCount = 0;
    QVector<QPair<QString, int>> participants;   // név, a címke hány megbeszélésén
    QStringList terms;
    QVector<QPair<QString, int>> cooccurring;    // tagId, hányszor együtt
    QVector<TagMeetingItem> meetings;            // legújabb elöl
};

// Egy (személy, címke) pár számokkal (core: PersonTagStat) — a Személyek ablak javaslat-sora,
// a Címkék ablak Személyek-listája és a személyválasztó rangsora.
struct PersonTagItem {
    QString name;               // a személy
    QString tagId;
    int shared = 0;             // közös megbeszélések c(p,t)
    int tagTotal = 0;           // a címke megbeszélései |M(t)|
    int personTotal = 0;        // a személy megbeszélései |M(p)|
    bool manual = false;        // a címke kézzel rajta van a személyen
    bool rejected = false;      // a (személy, címke) javaslat elutasítva
};

class TagBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~TagBackend() override = default;

    // ---- készlet ----
    virtual QVector<TagItem> tags() const = 0;                       // a teljes készlet
    virtual QVector<TagItem> recent(int limit) const = 0;            // legutóbb használtak
    // ---- egy megbeszélés címkéi ----
    virtual QStringList tagsOf(const QString& meetingId) const = 0;  // id-k a megbeszélés sorrendjében
    // Név vagy id; név esetén szükség szerint létrehozza. Visszaadja a címke id-jét.
    virtual QString addTag(const QString& meetingId, const QString& nameOrId, TagAddSource source) = 0;
    virtual void removeTag(const QString& meetingId, const QString& tagId) = 0;
    // ---- javaslatok ----
    virtual TagSuggestionState suggestions(const QString& meetingId) const = 0;
    virtual void requestSuggestions(const QString& meetingId) = 0;
    virtual void requestCooccur(const QString& meetingId, const QString& tagId) = 0;
    virtual void reject(const QString& meetingId, const TagSuggestionItem& suggestion) = 0;
    virtual bool isRejected(const QString& meetingId, const QString& tagIdOrName) const = 0;
    // Még nem létező megbeszélés (átirat előtti lépés, import) javaslatai a cím alapján;
    // szinkron. Alapból nincs ilyen.
    virtual QVector<TagSuggestionItem> draftSuggestions(const QString& title) const
    {
        Q_UNUSED(title);
        return {};
    }
    // ---- kezelő ----
    virtual TagProfileItem profile(const QString& tagId) const = 0;
    virtual bool rename(const QString& tagId, const QString& name) = 0;  // false: a név másé
    virtual void merge(const QString& fromId, const QString& keepId) = 0;
    virtual void remove(const QString& tagId) = 0;
    // ---- visszavonás: egy csoport = egy lépés ----
    virtual void beginGroup(const QString& label) = 0;
    virtual void endGroup() = 0;
    virtual bool canUndo() const = 0;
    virtual QString undoLabel() const = 0;
    virtual void undo() = 0;

    // ---- személyek (kézi címkék + tanult kapcsolat); alapból nincs ilyen adat ----
    // A saját személy neve (minden szabályból kimarad).
    virtual QString selfName() const { return {}; }
    virtual QStringList tagsOfPerson(const QString& name) const { Q_UNUSED(name); return {}; }
    // Név vagy id; név esetén szükség szerint létrehozza. A címke id-je (üres: nem sikerült).
    virtual QString addPersonTag(const QString& name, const QString& nameOrId)
    {
        Q_UNUSED(name); Q_UNUSED(nameOrId);
        return {};
    }
    virtual void removePersonTag(const QString& name, const QString& tagId) { Q_UNUSED(name); Q_UNUSED(tagId); }
    // Tartós (személy, címke) elutasítás (a javaslat eltűnik, a statisztika marad).
    virtual void rejectPersonTag(const QString& name, const QString& tagId) { Q_UNUSED(name); Q_UNUSED(tagId); }
    // Kézi címkéjükként viselik (név szerint, a saját személy nélkül).
    virtual QStringList peopleWith(const QString& tagId) const { Q_UNUSED(tagId); return {}; }
    // A címke személyei: kézi + tanult ≥ küszöb; kézi előbb, aztán a számláló szerint.
    virtual QVector<PersonTagItem> peopleStats(const QString& tagId) const { Q_UNUSED(tagId); return {}; }
    // „Ezeken szokott ott lenni” — mind (a nézet vágja); nincs rajta, nincs elutasítva.
    virtual QVector<PersonTagItem> suggestTagsForPerson(const QString& name) const { Q_UNUSED(name); return {}; }
    // A személy megbeszéléseinek száma a tanult statisztikában (|M(p)|).
    virtual int personMeetingCount(const QString& name) const { Q_UNUSED(name); return 0; }
    // A tanult statisztika épp számolódik (az első eredmény még nincs meg).
    virtual bool personStatsBusy() const { return false; }

signals:
    void tagsChanged();
    void meetingTagsChanged(const QString& meetingId);
    void suggestionsChanged(const QString& meetingId);
    void undoChanged();
    // Egy személy kézi címkéi / elutasításai változtak (üres név: több is, vagy ismeretlen).
    void personTagsChanged(const QString& name);
    // A tanult kapcsolat statisztikája frissült (vagy elkezdett számolódni).
    void personStatsChanged();
};

// A controllerre (tanara::AppController) épülő backend (TagControllerBackend). nullptr, ha a
// controller nem AppController — ilyenkor a nézetmodellek üres (adat nélküli) demó-backendet használnak.
TagBackend* createControllerTagBackend(QObject* controller, QObject* parent);

// A nézetmodellek közös választása: controller nélkül a kitalált demó-készlet; controllerrel a
// fenti gyár eredménye (ha még nincs ilyen: üres backend, hogy valódi adat helyett ne demó látsszon).
TagBackend* createTagBackend(QObject* controller, QObject* parent);

} // namespace tanara_qml
