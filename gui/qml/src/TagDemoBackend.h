#pragma once
//
// TagDemoBackend — a TagBackend kitalált adatos megvalósítása (controller nélkül: --qml-shot,
// --gallery, --demo, tesztek). A 12 címke, a számok és a nevek a handoff képeiről valók
// (design/handoff-tags, T01–T13), mind kitaláltak.
//
// Memóriában él, de VALÓDI szabályokkal: hozzáadás / eltávolítás / elutasítás / átnevezés /
// összevonás / törlés módosítja az állapotot, az együtt járás (≥ 2 megbeszélés, a címke
// megbeszéléseinek ≥ 50 %-a) számolódik, és minden csoport egy visszavonható lépés
// (pillanatkép-alapú verem, legfeljebb 50).
//
#include "DemoText.h"
#include "TagBackend.h"

#include <QHash>
#include <QSet>

namespace tanara_qml {

class TagDemoBackend : public TagBackend {
    Q_OBJECT
public:
    enum class Content { Sample, Empty };
    explicit TagDemoBackend(QObject* parent = nullptr) : TagDemoBackend(Content::Sample, parent) {}
    TagDemoBackend(Content content, QObject* parent);

    // A demó megbeszélés azonosítója (a MeetingTagsModel ezt használja, ha nincs meetingId).
    static QString demoMeetingId() { return QStringLiteral("m-demo"); }
    // Egy megbeszélés demó-állapota (a T01 C03–C04 sorai + a T02 „why”):
    // none | few | many | computing | similar | cooccur | llm | why | draft (T07: átirat előtt)
    void loadMeetingDemo(const QString& meetingId, const QString& state);
    // T13: üres készlet.
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
    // T07: a cím alapú javaslat (a címtől függetlenül ugyanaz a kitalált elem).
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

    // Személyek (a design-csomagok kitalált nevei; a saját személy: Kovács Lilla).
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
    // Képhez: a statisztika „még számol” állapota.
    void setPersonStatsBusy(bool busy);

private:
    struct State {
        QVector<TagItem> tags;
        QStringList recentIds;
        QHash<QString, QStringList> meetingTags;
        QHash<QString, TagSuggestionState> suggestions;
        QSet<QString> rejected;                     // "meetingId|tagId" vagy "meetingId|kulcs"
        QHash<QString, int> cooccur;                // "idA|idB" (idA < idB) → együtt
        QHash<QString, QStringList> personTags;     // név → kézi tagId-k
        QHash<QString, QHash<QString, int>> shared; // tagId → név → közös megbeszélések
        QHash<QString, int> personTotal;            // név → megbeszélések
        QSet<QString> personRejected;               // "név|tagId"
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

} // namespace tanara_qml
