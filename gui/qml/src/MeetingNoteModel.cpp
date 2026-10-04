#include "MeetingNoteModel.h"

#include "JobSupport.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/store/MeetingStore.h"

namespace tanara_qml {

using namespace tanara;

MeetingNoteModel::MeetingNoteModel(QObject* parent) : QObject(parent) {}

AppController* MeetingNoteModel::app() const
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
    // A még el nem mentett megjegyzés a RÉGI megbeszélésé: a váltás előtt oda írjuk ki.
    commitDraft();
    m_meetingId = id;
    reload();                 // előbb az új adatok, hogy a jelre már az új megjegyzés látsszon
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
    // A piszkozat egy már nem kijelölt megbeszélésé: közvetlenül oda mentjük.
    AppController* c = app();
    if (!jobsupport::demoMode(c) && !target.isEmpty())
        c->setMeetingContextNote(target, text);
}

void MeetingNoteModel::setNote(const QString& note)
{
    if (note == m_note)
        return;
    m_note = note;
    AppController* c = app();
    if (!jobsupport::demoMode(c) && !m_meetingId.isEmpty())
        c->setMeetingContextNote(m_meetingId, note);   // → meetingUpdated → a tulajdonos reload()-ja
    emit noteChanged();
    reloadSuggestions();   // a most bemásolt / beírt megjegyzés már nem javaslat
}

void MeetingNoteModel::applySuggestion(const QString& suggestion, const QString& mode)
{
    const QString text = suggestion.trimmed();
    if (text.isEmpty())
        return;
    // A közben gépelt, még nem mentett szöveg is a „meglévő” része.
    commitDraft();
    const QString current = m_note.trimmed();
    if (mode == QLatin1String("append") && !current.isEmpty())
        setNote(current + QLatin1Char('\n') + text);
    else
        setNote(text);
}

void MeetingNoteModel::reload()
{
    AppController* c = app();
    if (jobsupport::demoMode(c))
        return;   // a demó-tartalmat a tulajdonos adja (setDemoContent)
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
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        // Demóban is tűnjön el a már bemásolt javaslat (a képernyőkép-állapotokhoz elég).
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
        const QVector<meetingnotes::NoteSuggestion> hits =
            c->library()->noteSuggestions(m_meetingId, m_note, 3);
        for (const meetingnotes::NoteSuggestion& s : hits)
            list << QVariantMap{{QStringLiteral("meetingId"), s.meetingId},
                                {QStringLiteral("title"), s.title},
                                {QStringLiteral("dateText"), fmt::longDate(s.startedAt)},
                                {QStringLiteral("preview"), previewOf(s.note)},
                                {QStringLiteral("note"), s.note}};
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

// Kitalált sablon-javaslatok (demó): a „Nordvik heti meeting” korábbi alkalmai.
QVariantList MeetingNoteModel::demoSuggestions()
{
    auto item = [](const QString& title, const QString& date, const QString& note) {
        return QVariantMap{{QStringLiteral("meetingId"), QString()}, {QStringLiteral("title"), title},
                           {QStringLiteral("dateText"), date},
                           {QStringLiteral("preview"), previewOf(note)},
                           {QStringLiteral("note"), note}};
    };
    return {
        item(tr("Nordvik heti meeting"), tr("2026. szept. 29."),
             tr("Résztvevők: Kovács Anna, Szabó Bence, Lantos Réka. A „Nordwig” helyesen: Nordvik. "
                "A „kvarc modul” helyesen: Qvarko-modul. Téma: a bevezetés ütemezése.")),
        item(tr("Nordvik heti meeting – demó előtt"), tr("2026. szept. 22."),
             tr("A „pixel tár” helyesen: PixelTár. Résztvevők: Kovács Anna, Szabó Bence.")),
    };
}

} // namespace tanara_qml
