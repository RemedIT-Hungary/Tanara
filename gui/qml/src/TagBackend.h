#pragma once
//
// TagBackend: the SEAM between the tag view models (TagInputModel, MeetingTagsModel,
// TagsViewModel) and the data source (gui/qml/CONTRACT-TAGS.md, "Controls").
//
// Only the demo-data TagDemoBackend exists (TagDemoBackend.h). A controller-based variant
// (tanara::TagService + AppController suggestion signals) plugs in through the
// createControllerTagBackend() factory, without touching the view models.
//
// Deliberately Qt-typed and core-independent: the view models, the tests' mocks and the demo
// see the same interface. Ids are the core's (QUuid without braces); in the demo they look
// like "t-..." / "m-...".
//
#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara_qml {

// A tag from the pool, with usage data (core: TagUsage).
struct TagItem {
    QString id;
    QString name;
    int meetingCount = 0;
    QDateTime firstUsedAt;
    QDateTime lastUsedAt;
};

// Where the tag on the meeting came from (core: TagSource).
enum class TagAddSource { Manual, Suggestion, Llm, Bulk };

// A reason row in the "Why?" panel (core: SuggestionReason).
struct TagReasonItem {
    QString kind;               // "participant" | "terms" | "title" | "peopleTags"
    QStringList values;
};

// A reference to a meeting (core: MeetingRef + the length for the manager's list).
struct TagMeetingItem {
    QString meetingId;
    QString title;
    QDateTime startedAt;
    qint64 durationMs = 0;
};

// A suggestion (core: TagSuggestion).
struct TagSuggestionItem {
    QString tagId;              // empty if isNew
    QString name;
    bool isNew = false;         // the language model's new name, not yet in the pool
    QString source;             // "similar" | "cooccur" | "llm" | "people"
    double score = 0.0;
    QVector<TagReasonItem> reasons;
    QVector<TagMeetingItem> similarMeetings;
};

// A meeting's suggestion state: the latest result (from any source) + whether a computation runs.
struct TagSuggestionState {
    bool computing = false;
    QString source;             // "similar" | "cooccur" | "llm" | "people" (the items' source)
    QString baseTagId;          // cooccur: next to which tag it "often appears"
    QVector<TagSuggestionItem> items;
};

// The manager's right side (core: TagProfile).
struct TagProfileItem {
    QString tagId;
    int meetingCount = 0;
    QVector<QPair<QString, int>> participants;   // name, on how many meetings of the tag
    QStringList terms;
    QVector<QPair<QString, int>> cooccurring;    // tagId, how many times together
    QVector<TagMeetingItem> meetings;            // newest first
};

// A (person, tag) pair with numbers (core: PersonTagStat): the People window's suggestion
// row, the Tags window's People list and the person picker's ranking.
struct PersonTagItem {
    QString name;               // the person
    QString tagId;
    int shared = 0;             // shared meetings c(p,t)
    int tagTotal = 0;           // the tag's meetings |M(t)|
    int personTotal = 0;        // the person's meetings |M(p)|
    bool manual = false;        // the tag is on the person manually
    bool rejected = false;      // the (person, tag) suggestion was rejected
};

class TagBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~TagBackend() override = default;

    // ---- pool ----
    virtual QVector<TagItem> tags() const = 0;                       // the whole pool
    virtual QVector<TagItem> recent(int limit) const = 0;            // recently used
    // ---- a meeting's tags ----
    virtual QStringList tagsOf(const QString& meetingId) const = 0;  // ids in the meeting's order
    // Name or id; for a name, creates it if needed. Returns the tag's id.
    virtual QString addTag(const QString& meetingId, const QString& nameOrId, TagAddSource source) = 0;
    virtual void removeTag(const QString& meetingId, const QString& tagId) = 0;
    // ---- suggestions ----
    virtual TagSuggestionState suggestions(const QString& meetingId) const = 0;
    virtual void requestSuggestions(const QString& meetingId) = 0;
    virtual void requestCooccur(const QString& meetingId, const QString& tagId) = 0;
    virtual void reject(const QString& meetingId, const TagSuggestionItem& suggestion) = 0;
    virtual bool isRejected(const QString& meetingId, const QString& tagIdOrName) const = 0;
    // Suggestions by title for a meeting that does not exist yet (pre-transcript step, import);
    // synchronous. None by default.
    virtual QVector<TagSuggestionItem> draftSuggestions(const QString& title) const
    {
        Q_UNUSED(title);
        return {};
    }
    // ---- manager ----
    virtual TagProfileItem profile(const QString& tagId) const = 0;
    virtual bool rename(const QString& tagId, const QString& name) = 0;  // false: the name belongs to another tag
    virtual void merge(const QString& fromId, const QString& keepId) = 0;
    virtual void remove(const QString& tagId) = 0;
    // ---- undo: one group = one step ----
    virtual void beginGroup(const QString& label) = 0;
    virtual void endGroup() = 0;
    virtual bool canUndo() const = 0;
    virtual QString undoLabel() const = 0;
    virtual void undo() = 0;

    // ---- people (manual tags + learned relation); no such data by default ----
    // The own person's name (excluded from every rule).
    virtual QString selfName() const { return {}; }
    virtual QStringList tagsOfPerson(const QString& name) const { Q_UNUSED(name); return {}; }
    // Name or id; for a name, creates it if needed. The tag's id (empty: failed).
    virtual QString addPersonTag(const QString& name, const QString& nameOrId)
    {
        Q_UNUSED(name); Q_UNUSED(nameOrId);
        return {};
    }
    virtual void removePersonTag(const QString& name, const QString& tagId) { Q_UNUSED(name); Q_UNUSED(tagId); }
    // Lasting (person, tag) rejection (the suggestion disappears, the statistics stay).
    virtual void rejectPersonTag(const QString& name, const QString& tagId) { Q_UNUSED(name); Q_UNUSED(tagId); }
    // They wear it as their manual tag (by name, without the own person).
    virtual QStringList peopleWith(const QString& tagId) const { Q_UNUSED(tagId); return {}; }
    // The tag's people: manual + learned >= threshold; manual first, then by count.
    virtual QVector<PersonTagItem> peopleStats(const QString& tagId) const { Q_UNUSED(tagId); return {}; }
    // "Usually there at these": all of them (the view trims); not on them, not rejected.
    virtual QVector<PersonTagItem> suggestTagsForPerson(const QString& name) const { Q_UNUSED(name); return {}; }
    // The number of the person's meetings in the learned statistics (|M(p)|).
    virtual int personMeetingCount(const QString& name) const { Q_UNUSED(name); return 0; }
    // The learned statistics are being computed (the first result is not there yet).
    virtual bool personStatsBusy() const { return false; }

signals:
    void tagsChanged();
    void meetingTagsChanged(const QString& meetingId);
    void suggestionsChanged(const QString& meetingId);
    void undoChanged();
    // A person's manual tags / rejections changed (empty name: several, or unknown).
    void personTagsChanged(const QString& name);
    // The learned relation's statistics were refreshed (or started computing).
    void personStatsChanged();
};

// The controller-based backend (TagControllerBackend). nullptr if the controller is not an
// AppController; the view models then use an empty (data-less) demo backend.
TagBackend* createControllerTagBackend(QObject* controller, QObject* parent);

// The view models' shared choice: without a controller the fictional demo pool; with a
// controller the factory's result above (if none yet: an empty backend, so no demo shows instead of real data).
TagBackend* createTagBackend(QObject* controller, QObject* parent);

} // namespace tanara_qml
