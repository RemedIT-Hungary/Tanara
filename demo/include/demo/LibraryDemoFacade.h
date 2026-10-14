#pragma once
//
// LibraryDemoFacade: the fictional tanara::library::ILibraryFacade over the sample library
// (LibraryDemoData) for --demo, --gallery, --qml-shot and the tests. Bulk tagging changes the
// sample library's tags in memory (the signals follow, as with the real tag store).
#include "tanara/library/ILibraryFacade.h"

namespace tanara_demo {

class LibraryDemoFacade : public tanara::library::ILibraryFacade {
    Q_OBJECT
public:
    explicit LibraryDemoFacade(QObject* parent = nullptr);

    tanara::library::LibraryResult query(const tanara::library::LibraryQuery& q) const override;
    tanara::library::LibraryEntry entry(const QString& meetingId) const override;
    QVector<tanara::library::PersonPresence> people() const override;
    QVector<tanara::TagUsage> tagOptions() const override;
    int untaggedCount() const override;
    QVector<tanara::library::PendingItem> pendingItems(int limit = 0) const override;
    QString tagName(const QString& tagId) const override;
    bool isWarm() const override { return true; }
    void warmUp() override {}
    bool addTagToMeetings(const QStringList& meetingIds, const QString& nameOrId,
                          const QString& undoLabel) override;
    bool removeTagFromMeetings(const QStringList& meetingIds, const QString& tagId,
                               const QString& undoLabel) override;
};

} // namespace tanara_demo
