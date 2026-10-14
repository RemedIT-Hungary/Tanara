#include "demo/ParticipantsDemoFacade.h"

#include "demo/DemoText.h"

#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

using namespace tanara;
using namespace tanara::editing;

namespace tanara_demo {

namespace {

Evidence ev(EvidenceKind k, Polarity p, const QString& text)
{
    Evidence e;
    e.kind = k;
    e.polarity = p;
    e.text = text;
    return e;
}

} // namespace

void ParticipantsDemoFacade::applyDemoState(const QString& state)
{
    if (state == m_state) return;
    m_state = state;
}

ParticipantsState ParticipantsDemoFacade::state(const QString& meetingId) const
{
    Q_UNUSED(meetingId);
    const QString s = m_state.isEmpty() ? QStringLiteral("doubts") : m_state;
    ParticipantsState st;
    st.found = true;
    st.running = s == QLatin1String("running");
    st.hasTranscript = s != QLatin1String("noTranscriptYet") && !st.running;
    st.approvalPending = !st.running;
    // The models of the analysis: the recommended default.
    st.analysisModelIds = processing::VoiceModelRegistry::defaultEnabledIds();
    st.analysisAt = QDateTime::currentDateTime().addSecs(-190);   // "3 minutes ago"
    st.selfName = demoName("Kovács Lilla");
    st.tagIds = {QStringLiteral("demo-nordvik")};

    const auto S = Polarity::Support, N = Polarity::Neutral, X = Polarity::Contradict;
    auto row = [&st](const QString& id, const QString& name, ParticipantGroup group, bool checked,
                     QVector<Evidence> evs, const QStringList& raw, const QStringList& sides) {
        ParticipantCandidate c;
        c.id = id;
        c.name = name;
        c.group = group;
        c.checked = checked;
        c.evidence = std::move(evs);
        c.rawIds = raw;
        c.sides = sides;
        // A heard candidate has a fictional 6 s sample (the silent demo engine "plays" it).
        if (!sides.isEmpty()) {
            const qint64 start = 42000 + 31000 * st.candidates.size();
            c.sample = {sides.contains(QStringLiteral("mic")) ? QStringLiteral("mic") : QStringLiteral("call"),
                        start, start + 6000};
        }
        st.candidates.append(c);
    };
    const QString mic = processing::participants::kSideMic, loop = processing::participants::kSideLoopback;
    if (st.running) {
        // During the analysis only participants added manually (on the Overview) show.
        row(QStringLiteral("m-lilla"), demoName("Kovács Lilla"), ParticipantGroup::Doubt, true,
            {ev(EvidenceKind::Manual, S, demoText("kézzel felvéve", "added manually"))}, {}, {});
        st.candidates.last().manual = true;
        return st;
    }

    row(QStringLiteral("v-lilla"), demoName("Kovács Lilla"), ParticipantGroup::Sure, true,
        {ev(EvidenceKind::Side, S, demoText("mikrofon", "microphone")), ev(EvidenceKind::Voice, S, demoText("hang 96%", "voice 96%"))},
        {demoRaw(QStringLiteral("Beszélő 1@mic"))}, {mic});
    row(QStringLiteral("v-gabor"), demoName("Fehér Gábor"), ParticipantGroup::Sure, true,
        {ev(EvidenceKind::Voice, S, demoText("hang 84%", "voice 84%")), ev(EvidenceKind::Calendar, S, demoText("naptár", "calendar")),
         ev(EvidenceKind::Tag, S, QStringLiteral("címke: Nordvik"))},
        {demoRaw(QStringLiteral("Beszélő 1@loopback"))}, {loop});
    row(QStringLiteral("v-arpad"), demoName("Varga Árpád"), ParticipantGroup::Sure, true,
        {ev(EvidenceKind::Voice, S, demoText("hang 81%", "voice 81%")), ev(EvidenceKind::Calendar, S, demoText("naptár", "calendar"))},
        {demoRaw(QStringLiteral("Beszélő 3"))}, {loop});
    if (s != QLatin1String("allSure")) {
        row(QStringLiteral("v-eszter"), demoName("Molnár Eszter"), ParticipantGroup::Doubt, true,
            {ev(EvidenceKind::Voice, S, demoText("hang 61%", "voice 61%")), ev(EvidenceKind::Calendar, S, demoText("naptár", "calendar"))},
            {demoRaw(QStringLiteral("Beszélő 2"))}, {loop});
        row(QStringLiteral("v-bence"), demoName("Tóth Bence"), ParticipantGroup::Doubt, false,
            {ev(EvidenceKind::Voice, S, demoText("hang 58%", "voice 58%")),
             ev(EvidenceKind::Calendar, X, demoText("nincs a meghívottak közt", "not on the invite list")),
             ev(EvidenceKind::Tag, X, demoText("más címkéken szokott lenni", "usually on other tags"))},
            {demoRaw(QStringLiteral("Beszélő 2"))}, {loop});
    }
    if (s == QLatin1String("invitedNotHeard") || s == QLatin1String("addPerson")
        || s == QLatin1String("noTranscriptYet")) {
        row(QStringLiteral("c-reka"), demoName("Lantos Réka"), ParticipantGroup::InvitedNotHeard, false,
            {ev(EvidenceKind::Calendar, N, demoText("naptár · elfogadta", "calendar · accepted")),
             ev(EvidenceKind::Voice, X, demoText("nincs hanglenyomata", "has no voiceprint"))},
            {}, {});
    }
    st.rawLabels = QStringList{demoRaw(QStringLiteral("Beszélő 1")), demoRaw(QStringLiteral("Beszélő 2")),
                               demoRaw(QStringLiteral("Beszélő 3")), demoRaw(QStringLiteral("Beszélő 4"))};
    if (s == QLatin1String("allSure"))
        st.rawLabels = {demoRaw(QStringLiteral("Beszélő 1")), demoRaw(QStringLiteral("Beszélő 3"))};
    if (!st.hasTranscript) {
        st.rawLabels.clear();
        for (ParticipantCandidate& c : st.candidates) c.rawIds.clear();
    }
    return st;
}

QVector<PersonInfo> ParticipantsDemoFacade::peopleDirectory() const
{
    // The picker's people (the design packages' fictional names): the Tags window's list is the same.
    const QList<std::pair<const char*, int>> rows{
        {"Bárány Gergely", 23}, {"Dénes Zsófia", 17}, {"Fehér Gábor", 16}, {"Gál Bence", 12},
        {"Kovács Lilla", 41}, {"Molnár Eszter", 0}, {"Szabó Bence", 8}, {"Tóth Eszter", 3}, {"Varga Árpád", 12}};
    QVector<PersonInfo> out;
    for (const auto& [name, n] : rows) {
        PersonInfo p;
        p.name = demoName(name);
        p.meetingCount = n;
        p.hasVoiceprint = n > 2;
        out.append(p);
    }
    return out;
}

QVector<PersonInfo> ParticipantsDemoFacade::dialogPeople() const
{
    return {
        {{}, demoName("Szabó Bence"), true, 2, 11, {}, {}, {}},
        {{}, demoName("Szabó Dóra"), false, 0, 2, {}, {}, {}},
        {{}, demoName("Nagy Péter"), true, 1, 6, {}, {}, {}},
        {{}, demoName("Kiss Anna"), true, 3, 9, {}, {}, {}},
    };
}

TagPeopleHint ParticipantsDemoFacade::suggestPeople(const QStringList&, const QVector<PersonId>&,
                                                    const QStringList& presentNames, int) const
{
    const QString name = demoName("Szabó Bence");
    const bool running = m_state == QLatin1String("running");
    if (running || presentNames.contains(name, Qt::CaseInsensitive)) return {};
    library::PersonTagStat st;
    st.name = name;
    st.shared = 11;
    st.tagTotal = 14;
    return {QStringLiteral("Nordvik"), {st}};
}

ApprovalOutcome ParticipantsDemoFacade::approve(const QString&, const QVector<ApprovedRow>&)
{
    ApprovalOutcome out;
    out.ok = true;
    // The question after approval (screenshot): the demo's people have no ids (the answer reaches nothing).
    if (m_state == QLatin1String("learnNewPerson")) {
        out.questions = {library::PersonTagQuestion{QStringLiteral("demo-nordvik"), QStringLiteral("Nordvik"),
                                                    {PersonId()}, {demoName("Szabó Bence")}, true}};
    } else if (m_state.startsWith(QLatin1String("learn"))) {
        out.questions = {library::PersonTagQuestion{QStringLiteral("demo-nordvik"), QStringLiteral("Nordvik"),
                                                    {PersonId(), PersonId()},
                                                    {demoName("Fehér Gábor"), demoName("Varga Árpád")}, false},
                         library::PersonTagQuestion{QStringLiteral("demo-partnerek"), demoText("Partnerek", "Partners"),
                                                    {PersonId()}, {demoName("Fehér Gábor")}, false}};
    }
    return out;
}

QVector<ParticipantTrackChoice> ParticipantsDemoFacade::selectableTracks(const QString&) const
{
    return {{QStringLiteral("mic"), demoText("Saját mikrofon", "My microphone"), QStringLiteral("mic")},
            {QStringLiteral("call"), demoText("Hívás hangja", "Call audio"), QStringLiteral("phone")}};
}

ParticipantTrackBinding ParticipantsDemoFacade::participantTracks(const QString&, const QString& id) const
{
    ParticipantTrackBinding b;
    const QStringList manual = m_tracksSeeded ? m_manualTracks.value(id)
                                              : (id == QLatin1String("p3") ? QStringList{QStringLiteral("mic"), QStringLiteral("call")}
                                                                           : QStringList());
    b.manual = !manual.isEmpty();
    b.trackIds = b.manual ? manual : QStringList{id == QLatin1String("p1") ? QStringLiteral("mic") : QStringLiteral("call")};
    return b;
}

bool ParticipantsDemoFacade::setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds)
{
    if (!m_tracksSeeded) {
        m_manualTracks.insert(QStringLiteral("p3"), {QStringLiteral("mic"), QStringLiteral("call")});
        m_tracksSeeded = true;
    }
    if (trackIds.isEmpty()) m_manualTracks.remove(id);
    else m_manualTracks.insert(id, trackIds);
    emit participantsChanged(meetingId);
    return true;
}

} // namespace tanara_demo
