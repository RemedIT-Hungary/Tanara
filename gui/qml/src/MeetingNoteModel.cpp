#include "MeetingNoteModel.h"

#include "demo/NoteDemoData.h"

#include "ShellFormat.h"

#include "tanara/library/IMeetingNoteFacade.h"

namespace tanara_qml {

using namespace tanara;

MeetingNoteModel::MeetingNoteModel(QObject* parent) : QObject(parent) {}

void MeetingNoteModel::setController(QObject* noteFacade)
{
    m_facade = qobject_cast<library::IMeetingNoteFacade*>(noteFacade);
}

void MeetingNoteModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    // The not-yet-saved note belongs to the OLD meeting: we write it there before the switch.
    commitDraft();
    m_meetingId = id;
    reload();                 // new data first, so the signal already shows the new note
    emit meetingIdChanged();
}

void MeetingNoteModel::draft(const QString& text)
{
    m_draft = text;
    m_draftMeetingId = m_meetingId;
    m_hasDraft = true;
}

void MeetingNoteModel::commitDraft()
{
    if (!m_hasDraft)
        return;
    m_hasDraft = false;
    const QString text = m_draft;
    const QString target = m_draftMeetingId;
    m_draft.clear();
    m_draftMeetingId.clear();
    if (target == m_meetingId) {
        setNote(text);
        return;
    }
    // The draft belongs to a meeting that is no longer selected: we save it directly there.
    if (m_facade && !target.isEmpty())
        m_facade->setNote(target, text);
}

void MeetingNoteModel::setNote(const QString& note)
{
    if (note == m_note)
        return;
    m_note = note;
    if (m_facade && !m_meetingId.isEmpty())
        m_facade->setNote(m_meetingId, note);   // -> meetingUpdated -> the owner's reload()
    emit noteChanged();
    reloadSuggestions();   // the note just pasted / typed is no longer a suggestion
}

void MeetingNoteModel::applySuggestion(const QString& suggestion, const QString& mode)
{
    const QString text = suggestion.trimmed();
    if (text.isEmpty())
        return;
    // The text typed meanwhile and not yet saved is part of the "existing" too.
    commitDraft();
    const QString current = m_note.trimmed();
    if (mode == QLatin1String("append") && !current.isEmpty())
        setNote(current + QLatin1Char('\n') + text);
    else
        setNote(text);
}

void MeetingNoteModel::reload()
{
    if (!m_facade)
        return;   // the owner provides the demo content (setDemoContent)
    const QString oldNote = m_note;
    const QString oldApp = m_detectedCallApp;
    if (m_meetingId.isEmpty()) {
        m_note.clear();
        m_detectedCallApp.clear();
    } else {
        const library::IMeetingNoteFacade::Note n = m_facade->note(m_meetingId);
        m_note = n.text;
        m_detectedCallApp = n.detectedCallApp;
    }
    if (m_note != oldNote)
        emit noteChanged();
    if (m_detectedCallApp != oldApp)
        emit changed();
    reloadSuggestions();
}

void MeetingNoteModel::reloadSuggestions()
{
    if (!m_facade) {
        // In the demo too, an already pasted suggestion should disappear (enough for the screenshot states).
        QVariantList keep;
        for (const QVariant& v : std::as_const(m_suggestions))
            if (v.toMap().value(QStringLiteral("note")).toString().simplified() != m_note.simplified())
                keep << v;
        if (keep.size() != m_suggestions.size()) {
            m_suggestions = keep;
            emit suggestionsChanged();
        }
        return;
    }
    QVariantList list;
    if (!m_meetingId.isEmpty()) {
        const QVector<library::meetingnotes::NoteSuggestion> hits = m_facade->suggestions(m_meetingId, m_note, 3);
        for (const library::meetingnotes::NoteSuggestion& s : hits)
            list << QVariantMap{{QStringLiteral("meetingId"), s.meetingId},
                                {QStringLiteral("title"), s.title},
                                {QStringLiteral("dateText"), fmt::longDate(s.startedAt)},
                                {QStringLiteral("preview"), previewOf(s.note)},
                                {QStringLiteral("note"), s.note},
                                {QStringLiteral("tags"), m_facade->tagNames(s.meetingId)}};
    }
    if (list != m_suggestions) {
        m_suggestions = list;
        emit suggestionsChanged();
    }
}

void MeetingNoteModel::setDemoContent(const QString& note, const QString& detectedCallApp,
                                      const QVariantList& suggestions)
{
    const bool noteDiff = note != m_note;
    m_note = note;
    m_detectedCallApp = detectedCallApp;
    m_suggestions = suggestions;
    if (noteDiff)
        emit noteChanged();
    emit suggestionsChanged();
    emit changed();
}

QString MeetingNoteModel::previewOf(const QString& note)
{
    QString s = note.simplified();
    constexpr int kMax = 90;
    if (s.size() > kMax)
        s = s.left(kMax - 2).trimmed() + QStringLiteral("…");
    return s;
}

// The fictional template suggestions of tanara_demo (the owners' demo states), as the list rows.
QVariantList MeetingNoteModel::demoSuggestions()
{
    QVariantList out;
    for (const tanara_demo::DemoNoteSuggestion& d : tanara_demo::noteSuggestions())
        out << QVariantMap{{QStringLiteral("meetingId"), QString()}, {QStringLiteral("title"), d.title},
                           {QStringLiteral("dateText"), d.dateText},
                           {QStringLiteral("preview"), previewOf(d.note)},
                           {QStringLiteral("note"), d.note},
                           {QStringLiteral("tags"), d.tags}};
    return out;
}

} // namespace tanara_qml
