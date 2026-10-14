#pragma once
//
// LibraryServicesFacade: the real tanara::library::ILibraryFacade, over the MeetingLibrary query
// layer and the TagService (no AppController). One per controller, built by
// AppContext::servicesFor().
//
#include "tanara/library/ILibraryFacade.h"

#include <QPointer>

namespace tanara::library { class TagService; }

namespace tanara_qml {

class LibraryServicesFacade : public tanara::library::ILibraryFacade {
    Q_OBJECT
public:
    // Either may be nullptr (then everything is empty / refused).
    LibraryServicesFacade(tanara::library::MeetingLibrary* library, tanara::library::TagService* tags,
                          QObject* parent = nullptr);

    tanara::library::LibraryResult query(const tanara::library::LibraryQuery& q) const override;
    tanara::library::LibraryEntry entry(const QString& meetingId) const override;
    QVector<tanara::library::PersonPresence> people() const override;
    QVector<tanara::TagUsage> tagOptions() const override;
    int untaggedCount() const override;
    QVector<tanara::library::PendingItem> pendingItems(int limit = 0) const override;
    QString tagName(const QString& tagId) const override;
    bool isWarm() const override;
    void warmUp() override;
    bool addTagToMeetings(const QStringList& meetingIds, const QString& nameOrId,
                          const QString& undoLabel) override;
    bool removeTagFromMeetings(const QStringList& meetingIds, const QString& tagId,
                               const QString& undoLabel) override;

private:
    QPointer<tanara::library::MeetingLibrary> m_library;
    QPointer<tanara::library::TagService> m_tags;
};

} // namespace tanara_qml
