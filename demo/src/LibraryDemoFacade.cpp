#include "demo/LibraryDemoFacade.h"

#include "demo/LibraryDemoData.h"

namespace tanara_demo {

LibraryDemoFacade::LibraryDemoFacade(QObject* parent) : tanara::library::ILibraryFacade(parent)
{
    // A change of the sample tags is a change of the tag pool for the views.
    connect(tagNotifier(), &DemoTagNotifier::changed, this, &LibraryDemoFacade::tagsChanged);
}

tanara::library::LibraryResult LibraryDemoFacade::query(const tanara::library::LibraryQuery& q) const
{
    return tanara_demo::query(q);
}

tanara::library::LibraryEntry LibraryDemoFacade::entry(const QString& meetingId) const
{
    return tanara_demo::entry(meetingId);
}

QVector<tanara::library::PersonPresence> LibraryDemoFacade::people() const
{
    return tanara_demo::people();
}

QVector<tanara::TagUsage> LibraryDemoFacade::tagOptions() const
{
    return tanara_demo::tagOptions();
}

int LibraryDemoFacade::untaggedCount() const
{
    return tanara_demo::untaggedCount();
}

QVector<tanara::library::PendingItem> LibraryDemoFacade::pendingItems(int limit) const
{
    QVector<tanara::library::PendingItem> all = tanara_demo::pendingItems();
    if (limit > 0 && all.size() > limit)
        all.resize(limit);
    return all;
}

QString LibraryDemoFacade::tagName(const QString& tagId) const
{
    return tanara_demo::tagName(tagId);
}

bool LibraryDemoFacade::addTagToMeetings(const QStringList& meetingIds, const QString& nameOrId,
                                         const QString&)
{
    const QString id = tanara_demo::tagName(nameOrId).isEmpty() ? tanara_demo::createTag(nameOrId) : nameOrId;
    if (id.isEmpty())
        return true;
    for (const QString& m : meetingIds) {
        QStringList t = tanara_demo::tagsOf(m);
        if (t.contains(id)) continue;
        tanara_demo::setTagsOf(m, t << id);
        emit meetingChanged(m);
    }
    return true;
}

bool LibraryDemoFacade::removeTagFromMeetings(const QStringList& meetingIds, const QString& tagId,
                                              const QString&)
{
    for (const QString& m : meetingIds) {
        QStringList t = tanara_demo::tagsOf(m);
        if (t.removeAll(tagId) == 0) continue;
        tanara_demo::setTagsOf(m, t);
        emit meetingChanged(m);
    }
    return true;
}

} // namespace tanara_demo
