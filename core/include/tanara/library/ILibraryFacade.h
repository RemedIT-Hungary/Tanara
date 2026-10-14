#pragma once
//
// ILibraryFacade: what the main window's library column (the list, the search and filters, the
// "Waiting for you" card, the multi-select panel) needs from the library, as ONE abstract
// surface. The real implementation wraps MeetingLibrary + TagService (gui/qml/src/
// LibraryServicesFacade); the fictional one (demo/LibraryDemoFacade) serves the sample library
// of --demo, --gallery, --qml-shot and the tests.
//
// It is a QObject only because it carries signals. The view models never learn which
// implementation they talk to.
#include "tanara/library/MeetingLibrary.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::library {

class ILibraryFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ILibraryFacade() override = default;

    // The same contract as MeetingLibrary: query / entry / people / tagOptions / untaggedCount /
    // pendingItems. entry(): an empty id if there is no such meeting.
    virtual LibraryResult query(const LibraryQuery& q) const = 0;
    virtual LibraryEntry entry(const QString& meetingId) const = 0;
    virtual QVector<PersonPresence> people() const = 0;
    virtual QVector<TagUsage> tagOptions() const = 0;
    virtual int untaggedCount() const = 0;
    virtual QVector<PendingItem> pendingItems(int limit = 0) const = 0;
    // A tag's name by id; empty if there is no such tag.
    virtual QString tagName(const QString& tagId) const = 0;

    // Preloading the transcript texts for the full-text search (a no-op where there is nothing to load).
    virtual bool isWarm() const = 0;
    virtual void warmUp() = 0;

    // Bulk tagging: one undoable step labelled `undoLabel`. addTagToMeetings takes a tag name or id
    // (a name that does not exist yet is created). False: nothing could be done (no tag store).
    virtual bool addTagToMeetings(const QStringList& meetingIds, const QString& nameOrId,
                                  const QString& undoLabel) = 0;
    virtual bool removeTagFromMeetings(const QStringList& meetingIds, const QString& tagId,
                                       const QString& undoLabel) = 0;

signals:
    // As MeetingLibrary's.
    void meetingAdded(QString meetingId);
    void meetingChanged(QString meetingId);
    void meetingRemoved(QString meetingId);
    void pendingItemsChanged();
    void reset();
    void tagsChanged();
    void warmedUp();
};

} // namespace tanara::library
