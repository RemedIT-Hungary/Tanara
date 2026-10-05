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
    QString kind;               // "participant" | "terms" | "title"
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
    QString source;             // "similar" | "cooccur" | "llm"
    double score = 0.0;
    QVector<TagReasonItem> reasons;
    QVector<TagMeetingItem> similarMeetings;
};

// Egy megbeszélés javaslat-állapota: a legutóbbi eredmény (bármely forrásból) + fut-e számolás.
struct TagSuggestionState {
    bool computing = false;
    QString source;             // "similar" | "cooccur" | "llm" (az items forrása)
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

signals:
    void tagsChanged();
    void meetingTagsChanged(const QString& meetingId);
    void suggestionsChanged(const QString& meetingId);
    void undoChanged();
};

// Wave 2 bekötési pontja: a controllerre (tanara::AppController) épülő backend. Wave 1-ben
// nullptr-t ad — ilyenkor a nézetmodellek üres (adat nélküli) demó-backendet használnak.
TagBackend* createControllerTagBackend(QObject* controller, QObject* parent);

// A nézetmodellek közös választása: controller nélkül a kitalált demó-készlet; controllerrel a
// fenti gyár eredménye (ha még nincs ilyen: üres backend, hogy valódi adat helyett ne demó látsszon).
TagBackend* createTagBackend(QObject* controller, QObject* parent);

} // namespace tanara_qml
