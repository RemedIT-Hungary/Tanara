#pragma once
//
// TagControllerBackend: the real implementation of the TagBackend seam: tanara::AppController +
// tanara::TagService (CONTRACT-TAGS.md, "Core API").
//
// Threading: every call and every signal runs on the GUI thread. The core builds the profiles
// on a background thread but emits the suggestion signals (tagSuggestionsComputing ->
// tagSuggestionsReady) on the GUI thread; this class only forwards them.
//
// Signal order: when a tag is attached, on TagService::meetingTagsChanged the controller
// FIRST re-emits the filtered suggestion list (tagSuggestionsReady -> suggestionsChanged),
// then the forwarded meetingTagsChanged follows; the view model refreshes on both, so the order does not matter.
//
// The suggestion state: per meeting the last FRESH list (preceded by a Computing). After an
// attach / reject the core re-emits the filtered list (without Computing); the backend does
// not take it over if it is a subset of the earlier one; the view model does the
// filtering, so after an undo the suggestion comes back. What has not arrived here yet comes
// from the controller's pendingTagSuggestions() (for a view model that connects later). The
// `computing` flag is true from the Computing signal until the next Ready; an item pointing
// to a deleted tag is dropped.
//
#include "TagBackend.h"
#include "tanara/domain/Person.h"

#include "tanara/domain/Tag.h"

#include <QHash>
#include <QPointer>
#include <QSet>

namespace tanara {
namespace app { class AppController; }
namespace library { class TagService; }
} // namespace tanara

namespace tanara_qml {

class TagControllerBackend : public TagBackend {
public:
    TagControllerBackend(tanara::app::AppController* controller, QObject* parent = nullptr);

    QVector<TagItem> tags() const override;
    QVector<TagItem> recent(int limit) const override;
    QStringList tagsOf(const QString& meetingId) const override;
    QString addTag(const QString& meetingId, const QString& nameOrId, TagAddSource source) override;
    void removeTag(const QString& meetingId, const QString& tagId) override;
    TagSuggestionState suggestions(const QString& meetingId) const override;
    void requestSuggestions(const QString& meetingId) override;
    void requestCooccur(const QString& meetingId, const QString& tagId) override;
    void reject(const QString& meetingId, const TagSuggestionItem& suggestion) override;
    bool isRejected(const QString& meetingId, const QString& tagIdOrName) const override;
    TagProfileItem profile(const QString& tagId) const override;
    bool rename(const QString& tagId, const QString& name) override;
    void merge(const QString& fromId, const QString& keepId) override;
    void remove(const QString& tagId) override;
    void beginGroup(const QString& label) override;
    void endGroup() override;
    bool canUndo() const override;
    QString undoLabel() const override;
    void undo() override;

    QString selfName() const override;
    QStringList tagsOfPerson(const QString& name) const override;
    QString addPersonTag(const QString& name, const QString& nameOrId) override;
    void removePersonTag(const QString& name, const QString& tagId) override;
    void rejectPersonTag(const QString& name, const QString& tagId) override;
    QStringList peopleWith(const QString& tagId) const override;
    QVector<PersonTagItem> peopleStats(const QString& tagId) const override;
    QVector<PersonTagItem> suggestTagsForPerson(const QString& name) const override;
    int personMeetingCount(const QString& name) const override;
    bool personStatsBusy() const override;

    // Draft (a meeting that does not exist yet, e.g. the pre-transcript step): by title, synchronous.
    QVector<TagSuggestionItem> draftSuggestions(const QString& title) const override;

    // core -> UI conversion (for tests and the draft suggestions too).
    static TagSuggestionItem toItem(const tanara::TagSuggestion& s);

private:
    tanara::library::TagService* service() const;
    // The GUI hands over a person's display name (or a PersonId): the person behind it, and back.
    tanara::PersonId personOf(const QString& nameOrId) const;
    QString nameOf(const tanara::PersonId& person) const;

    QPointer<tanara::app::AppController> m_controller;
    QSet<QString> m_computing;   // meetingIds for which no Ready has come after the Computing
    QHash<QString, QVector<tanara::TagSuggestion>> m_lists;   // meetingId -> last fresh list
};

} // namespace tanara_qml
