#include "MeetingNoteModel.h"

#include "DemoText.h"

#include "JobSupport.h"
#include "ShellFormat.h"

#include "tanara/app/AppController.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/tags/TagService.h"

namespace tanara_qml {

using namespace tanara;

namespace {

// The source meeting's tag names (for the card's mini chips), in the meeting's order.
QStringList tagNamesOf(app::AppController* c, const QString& meetingId)
{
    QStringList out;
    library::TagService* tags = c ? c->tags() : nullptr;
    if (!tags || meetingId.isEmpty()) return out;
    for (const QString& id : tags->tagsOf(meetingId)) {
        const Tag t = tags->tag(id);
        if (t.isValid()) out << t.name;
    }
    return out;
}

} // namespace

MeetingNoteModel::MeetingNoteModel(QObject* parent) : QObject(parent) {}

app::AppController* MeetingNoteModel::app() const
{
    return jobsupport::resolveController(m_injected);
}

bool MeetingNoteModel::demo() const { return jobsupport::demoMode(app()); }

void MeetingNoteModel::setController(QObject* injected)
{
    m_injected = injected;
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
    app::AppController* c = app();
    if (!jobsupport::demoMode(c) && !target.isEmpty())
        c->setMeetingContextNote(target, text);
}

void MeetingNoteModel::setNote(const QString& note)
{
    if (note == m_note)
        return;
    m_note = note;
    app::AppController* c = app();
    if (!jobsupport::demoMode(c) && !m_meetingId.isEmpty())
        c->setMeetingContextNote(m_meetingId, note);   // -> meetingUpdated -> the owner's reload()
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
    app::AppController* c = app();
    if (jobsupport::demoMode(c))
        return;   // the owner provides the demo content (setDemoContent)
    const QString oldNote = m_note;
    const QString oldApp = m_detectedCallApp;
    if (m_meetingId.isEmpty()) {
        m_note.clear();
        m_detectedCallApp.clear();
    } else {
        const Meeting m = c->store()->load(m_meetingId);
        m_note = m.contextNote;
        m_detectedCallApp = m.detectedCallApp;
    }
    if (m_note != oldNote)
        emit noteChanged();
    if (m_detectedCallApp != oldApp)
        emit changed();
    reloadSuggestions();
}

void MeetingNoteModel::reloadSuggestions()
{
    app::AppController* c = app();
    if (jobsupport::demoMode(c)) {
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
    if (!m_meetingId.isEmpty() && c->library()) {
        const QVector<library::meetingnotes::NoteSuggestion> hits =
            c->library()->noteSuggestions(m_meetingId, m_note, 3);
        for (const library::meetingnotes::NoteSuggestion& s : hits)
            list << QVariantMap{{QStringLiteral("meetingId"), s.meetingId},
                                {QStringLiteral("title"), s.title},
                                {QStringLiteral("dateText"), fmt::longDate(s.startedAt)},
                                {QStringLiteral("preview"), previewOf(s.note)},
                                {QStringLiteral("note"), s.note},
                                {QStringLiteral("tags"), tagNamesOf(c, s.meetingId)}};
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

// Fictional template suggestions (demo): earlier occurrences of the "Nordvik weekly meeting".
QVariantList MeetingNoteModel::demoSuggestions()
{
    auto item = [](const QString& title, const QString& date, const QString& note) {
        return QVariantMap{{QStringLiteral("meetingId"), QString()}, {QStringLiteral("title"), title},
                           {QStringLiteral("dateText"), date},
                           {QStringLiteral("preview"), previewOf(note)},
                           {QStringLiteral("note"), note},
                           {QStringLiteral("tags"), QStringList{QStringLiteral("Nordvik")}}};
    };
    return {
        item(demoText("Nordvik heti meeting", "Nordvik weekly meeting"), demoText("2026. szept. 29.", "Sep 29, 2026"),
             demoText("Résztvevők: Kovács Anna, Szabó Bence, Lantos Réka. A „Nordwig” helyesen: Nordvik. "
                "A „kvarc modul” helyesen: Qvarko-modul. Téma: a bevezetés ütemezése.", "Participants: Anna Carter, Noah Turner, Rachel Lane. “Nordwig” is really Nordvik. “quark module” is really Qvarko module. Topic: rollout schedule.")),
        item(demoText("Nordvik heti meeting – demó előtt", "Nordvik weekly meeting – before the demo"), demoText("2026. szept. 22.", "Sep 22, 2026"),
             demoText("A „pixel tár” helyesen: PixelTár. Résztvevők: Kovács Anna, Szabó Bence.", "“pixel vault” is really PixelVault. Participants: Anna Carter, Noah Turner.")),
    };
}

} // namespace tanara_qml
