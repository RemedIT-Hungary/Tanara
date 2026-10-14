#include "MeetingNoteServicesFacade.h"

#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

using namespace tanara;

namespace tanara_qml {

MeetingNoteServicesFacade::MeetingNoteServicesFacade(const Deps& deps, QObject* parent)
    : IMeetingNoteFacade(parent), m_deps(deps)
{
}

library::IMeetingNoteFacade::Note MeetingNoteServicesFacade::note(const QString& meetingId) const
{
    Note out;
    if (!m_deps.store || meetingId.isEmpty()) return out;
    const Meeting m = m_deps.store->load(meetingId);
    out.text = m.contextNote;
    out.detectedCallApp = m.detectedCallApp;
    return out;
}

void MeetingNoteServicesFacade::setNote(const QString& meetingId, const QString& text)
{
    if (m_deps.setNote) m_deps.setNote(meetingId, text);
}

QVector<library::meetingnotes::NoteSuggestion> MeetingNoteServicesFacade::suggestions(const QString& meetingId,
                                                                                      const QString& currentNote,
                                                                                      int limit) const
{
    if (!m_deps.library || meetingId.isEmpty()) return {};
    return m_deps.library->noteSuggestions(meetingId, currentNote, limit);
}

QStringList MeetingNoteServicesFacade::tagNames(const QString& meetingId) const
{
    QStringList out;
    if (!m_deps.tags || meetingId.isEmpty()) return out;
    for (const QString& id : m_deps.tags->tagsOf(meetingId)) {
        const Tag t = m_deps.tags->tag(id);
        if (t.isValid()) out << t.name;
    }
    return out;
}

} // namespace tanara_qml
