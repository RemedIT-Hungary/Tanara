#pragma once
//
// MeetingNoteServicesFacade: the real tanara::library::IMeetingNoteFacade over the meeting
// repository, MeetingLibrary (the template suggestions) and the tag service. Saving the note (with
// its undo step) still lives on the AppController: it comes in as a hook. One per controller,
// built by AppContext::servicesFor().
//
#include "tanara/library/IMeetingNoteFacade.h"

#include <functional>

namespace tanara::library { class MeetingLibrary; class MeetingRepository; class TagService; }

namespace tanara_qml {

class MeetingNoteServicesFacade : public tanara::library::IMeetingNoteFacade {
    Q_OBJECT
public:
    struct Deps {
        tanara::library::MeetingRepository* store = nullptr;
        tanara::library::MeetingLibrary* library = nullptr;
        tanara::library::TagService* tags = nullptr;
        std::function<void(const QString& meetingId, const QString& text)> setNote;
    };
    explicit MeetingNoteServicesFacade(const Deps& deps, QObject* parent = nullptr);

    Note note(const QString& meetingId) const override;
    void setNote(const QString& meetingId, const QString& text) override;
    QVector<tanara::library::meetingnotes::NoteSuggestion> suggestions(const QString& meetingId,
                                                                       const QString& currentNote,
                                                                       int limit) const override;
    QStringList tagNames(const QString& meetingId) const override;

private:
    Deps m_deps;
};

} // namespace tanara_qml
