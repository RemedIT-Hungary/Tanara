#pragma once
//
// TagDemoBackend: the fictional-data implementation of TagBackend (--qml-shot, --gallery, --demo,
// tests; UiServices::tagBackend of the demo set). The 12 tags (demo/TagPool.h), the numbers and
// the names come from the handoff images, all fictional.
//
// It lives in memory but with REAL rules: add / remove / reject / rename / merge / delete
// change the state, co-occurrence (>= 2 meetings, >= 50 % of the tag's meetings) is
// computed, and every group is one undoable step (snapshot-based stack, at most 50).
//
#include "demo/DemoText.h"

#include "tanara/library/tags/TagBackend.h"

#include <QHash>
#include <QSet>

namespace tanara_demo {

using tanara::library::PersonTagItem;
using tanara::library::TagAddSource;
using tanara::library::TagBackend;
using tanara::library::TagItem;
using tanara::library::TagMeetingItem;
using tanara::library::TagReasonItem;
using tanara::library::TagProfileItem;
using tanara::library::TagSuggestionItem;
using tanara::library::TagSuggestionState;

class TagDemoBackend : public TagBackend {
    Q_OBJECT
public:
    enum class Content { Sample, Empty };
    explicit TagDemoBackend(QObject* parent = nullptr) : TagDemoBackend(Content::Sample, parent) {}
    TagDemoBackend(Content content, QObject* parent);

    // The demo meeting's id (MeetingTagsModel uses it when there is no meetingId).
    static QString demoMeetingId() { return QStringLiteral("m-demo"); }
    // A meeting's demo state (the T01 rows + the T02 "why"):
    // none | few | many | computing | similar | cooccur | llm | why | draft (before a transcript)
    void loadMeetingDemo(const QString& meetingId, const QString& state);
    // Empty pool.
    void clearAll();

    QVector<TagItem> tags() const override { return m_state.tags; }
    QVector<TagItem> recent(int limit) const override;
    QStringList tagsOf(const QString& meetingId) const override { return m_state.meetingTags.value(meetingId); }
    QString addTag(const QString& meetingId, const QString& nameOrId, TagAddSource source) override;
    void removeTag(const QString& meetingId, const QString& tagId) override;
    TagSuggestionState suggestions(const QString& meetingId) const override { return m_state.suggestions.value(meetingId); }
    void requestSuggestions(const QString& meetingId) override;
    void requestCooccur(const QString& meetingId, const QString& tagId) override;
    void reject(const QString& meetingId, const TagSuggestionItem& suggestion) override;
    bool isRejected(const QString& meetingId, const QString& tagIdOrName) const override;
    // The title-based suggestion (the same fictional item regardless of the title).
    QVector<TagSuggestionItem> draftSuggestions(const QString& title) const override;
    TagProfileItem profile(const QString& tagId) const override;
    bool rename(const QString& tagId, const QString& name) override;
    void merge(const QString& fromId, const QString& keepId) override;
    void remove(const QString& tagId) override;
    void beginGroup(const QString& label) override;
    void endGroup() override;
    bool canUndo() const override { return !m_undo.isEmpty(); }
    QString undoLabel() const override { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    void undo() override;

    // People (the design packages' fictional names; the own person: Kovács Lilla).
    QString selfName() const override { return demoName("Kovács Lilla"); }
    QStringList tagsOfPerson(const QString& name) const override;
    QString addPersonTag(const QString& name, const QString& nameOrId) override;
    void removePersonTag(const QString& name, const QString& tagId) override;
    void rejectPersonTag(const QString& name, const QString& tagId) override;
    QStringList peopleWith(const QString& tagId) const override;
    QVector<PersonTagItem> peopleStats(const QString& tagId) const override;
    QVector<PersonTagItem> suggestTagsForPerson(const QString& name) const override;
    int personMeetingCount(const QString& name) const override { return m_state.personTotal.value(name); }
    bool personStatsBusy() const override { return m_statsBusy; }
    // For images: the statistics' "still computing" state.
    void setPersonStatsBusy(bool busy);

private:
    struct State {
        QVector<TagItem> tags;
        QStringList recentIds;
        QHash<QString, QStringList> meetingTags;
        QHash<QString, TagSuggestionState> suggestions;
        QSet<QString> rejected;                     // "meetingId|tagId" or "meetingId|key"
        QHash<QString, int> cooccur;                // "idA|idB" (idA < idB) -> together
        QHash<QString, QStringList> personTags;     // name -> manual tagIds
        QHash<QString, QHash<QString, int>> shared; // tagId -> name -> shared meetings
        QHash<QString, int> personTotal;            // name -> meetings
        QSet<QString> personRejected;               // "name|tagId"
    };
    struct UndoStep { QString label; State before; };

    int indexOf(const QString& tagId) const;
    QString idOf(const QString& nameOrId) const;
    static QString pairKey(const QString& a, const QString& b);
    QVector<QPair<QString, int>> cooccurring(const QString& tagId) const;
    void touch(const QString& fallbackLabel);
    void emitAll();
    PersonTagItem personItem(const QString& name, const QString& tagId) const;

    State m_state;
    QVector<UndoStep> m_undo;
    int m_groupDepth = 0;
    QString m_groupLabel;
    bool m_groupRecorded = false;
    int m_created = 0;
    bool m_statsBusy = false;
};

} // namespace tanara_demo
