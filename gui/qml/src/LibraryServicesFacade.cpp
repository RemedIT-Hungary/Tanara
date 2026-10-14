#include "LibraryServicesFacade.h"

#include "tanara/library/tags/TagService.h"

namespace tanara_qml {

using tanara::library::MeetingLibrary;

LibraryServicesFacade::LibraryServicesFacade(MeetingLibrary* library, tanara::library::TagService* tags,
                                             QObject* parent)
    : tanara::library::ILibraryFacade(parent), m_library(library), m_tags(tags)
{
    if (!library)
        return;
    // The library's signals are the facade's signals.
    connect(library, &MeetingLibrary::meetingAdded, this, &LibraryServicesFacade::meetingAdded);
    connect(library, &MeetingLibrary::meetingChanged, this, &LibraryServicesFacade::meetingChanged);
    connect(library, &MeetingLibrary::meetingRemoved, this, &LibraryServicesFacade::meetingRemoved);
    connect(library, &MeetingLibrary::pendingItemsChanged, this, &LibraryServicesFacade::pendingItemsChanged);
    connect(library, &MeetingLibrary::reset, this, &LibraryServicesFacade::reset);
    connect(library, &MeetingLibrary::tagsChanged, this, &LibraryServicesFacade::tagsChanged);
    connect(library, &MeetingLibrary::warmedUp, this, &LibraryServicesFacade::warmedUp);
}

tanara::library::LibraryResult LibraryServicesFacade::query(const tanara::library::LibraryQuery& q) const
{
    return m_library ? m_library->query(q) : tanara::library::LibraryResult{};
}

tanara::library::LibraryEntry LibraryServicesFacade::entry(const QString& meetingId) const
{
    return m_library ? m_library->entry(meetingId) : tanara::library::LibraryEntry{};
}

QVector<tanara::library::PersonPresence> LibraryServicesFacade::people() const
{
    return m_library ? m_library->people() : QVector<tanara::library::PersonPresence>{};
}

QVector<tanara::TagUsage> LibraryServicesFacade::tagOptions() const
{
    return m_library ? m_library->tagOptions() : QVector<tanara::TagUsage>{};
}

int LibraryServicesFacade::untaggedCount() const
{
    return m_library ? m_library->untaggedCount() : 0;
}

QVector<tanara::library::PendingItem> LibraryServicesFacade::pendingItems(int limit) const
{
    return m_library ? m_library->pendingItems(limit) : QVector<tanara::library::PendingItem>{};
}

QString LibraryServicesFacade::tagName(const QString& tagId) const
{
    return m_tags ? m_tags->tag(tagId).name : QString();
}

bool LibraryServicesFacade::isWarm() const
{
    return !m_library || m_library->isWarm();
}

void LibraryServicesFacade::warmUp()
{
    if (m_library && !m_library->isWarm())
        m_library->warmUp();
}

bool LibraryServicesFacade::addTagToMeetings(const QStringList& meetingIds, const QString& nameOrId,
                                             const QString& undoLabel)
{
    if (!m_tags)
        return false;
    // Creating and attaching together are one undoable step.
    tanara::Tag t = m_tags->tag(nameOrId);
    m_tags->beginGroup(undoLabel);
    if (!t.isValid()) t = m_tags->create(nameOrId);
    if (t.isValid()) m_tags->bulkAdd(meetingIds, t.id);
    m_tags->endGroup();
    return true;
}

bool LibraryServicesFacade::removeTagFromMeetings(const QStringList& meetingIds, const QString& tagId,
                                                  const QString& undoLabel)
{
    if (!m_tags)
        return false;
    m_tags->beginGroup(undoLabel);
    m_tags->bulkRemove(meetingIds, tagId);
    m_tags->endGroup();
    return true;
}

} // namespace tanara_qml
