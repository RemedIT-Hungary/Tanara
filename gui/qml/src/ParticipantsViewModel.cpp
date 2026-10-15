#include "ParticipantsViewModel.h"

#include "AppContext.h"
#include "TagLearning.h"

#include "tanara/editing/participants/IParticipantsFacade.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QDateTime>

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

namespace {

// Short model name for the meta line: "CAM++ (3D-Speaker)" -> "CAM++".
QString shortModelName(const QString& id)
{
    const auto spec = processing::VoiceModelRegistry::spec(id);
    const QString name = spec ? spec->displayName : id;
    return name.section(QLatin1Char(' '), 0, 0);
}

// "3 min ago" / "2 hours ago" / "yesterday" / "Oct 1": the age of the audio analysis.
QString ageText(const QDateTime& at)
{
    if (!at.isValid()) return {};
    const qint64 secs = at.secsTo(QDateTime::currentDateTime());
    if (secs < 60) return ParticipantsViewModel::tr("most");
    if (secs < 3600) return ParticipantsViewModel::tr("%n perce", "", int(secs / 60));
    if (secs < 24 * 3600) return ParticipantsViewModel::tr("%n órája", "", int(secs / 3600));
    if (at.date().daysTo(QDate::currentDate()) == 1) return ParticipantsViewModel::tr("tegnap");
    return QLocale(QLocale::Hungarian).toString(at.date(), QStringLiteral("MMM d."));
}

} // namespace

ParticipantsViewModel::ParticipantsViewModel(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { resolveServices(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, [this]() { resolveServices(); reload(); });
    resolveServices();
    reload();
}

ParticipantsViewModel::ParticipantsViewModel(const UiServices& services, QObject* parent)
    : QObject(parent), m_explicitServices(true)
{
    useServices(services);
    reload();
}

bool ParticipantsViewModel::demo() const { return m_facade->fictional(); }

void ParticipantsViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId) return;
    m_meetingId = id;
    emit meetingIdChanged();
    reload();
}

void ParticipantsViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState) return;
    m_demoState = state;
    m_facade->applyDemoState(state);
    emit demoStateChanged();
    reload();
}

// The process's services changed (a controller arrived, App.demo flipped).
void ParticipantsViewModel::resolveServices()
{
    if (m_explicitServices) return;
    useServices(AppContext::instance()->services());
}

void ParticipantsViewModel::useServices(const UiServices& services)
{
    for (const auto& conn : std::as_const(m_connections)) disconnect(conn);
    m_connections.clear();
    if (m_facade && m_facade->parent() == this) m_facade->deleteLater();   // a fictional facade of ours
    m_facade = services.participants(this);
    m_facade->applyDemoState(m_demoState);
    // End of the analysis / change of the meeting's participants: the groups are recomputed. The
    // user's checks stay (the working copy is restored by id).
    auto refreshFor = [this](const QString& meetingId) {
        if (meetingId != m_meetingId || demo()) return;
        load();
    };
    m_connections << connect(m_facade, &editing::IParticipantsFacade::participantsChanged, this, refreshFor);
    m_connections << connect(m_facade, &editing::IParticipantsFacade::participantAnalysisStarted, this, refreshFor);
    m_connections << connect(m_facade, &editing::IParticipantsFacade::participantAnalysisFinished, this, refreshFor);
}

void ParticipantsViewModel::reload()
{
    m_rows.clear();
    load();
}

void ParticipantsViewModel::load()
{
    // The working copy: the current checks / additions / names are kept by id.
    const QVector<Row> previous = m_rows;

    m_rows.clear();
    m_running = false;
    m_hasTranscript = false;
    m_pending = false;
    m_candidateCount = 0;
    m_metaText.clear();

    const editing::ParticipantsState st = m_facade->state(m_meetingId);
    if (!st.found) {
        emit changed();
        return;
    }
    m_running = st.running;
    m_pending = st.approvalPending;
    m_hasTranscript = st.hasTranscript;

    if (!st.analysisModelIds.isEmpty() || st.analysisAt.isValid()) {
        QStringList models;
        for (const QString& id : st.analysisModelIds) models << shortModelName(id);
        QStringList parts{tr("hanglenyomat-elemzés")};
        if (!models.isEmpty()) parts << models.join(QStringLiteral(" + "));
        const QString age = ageText(st.analysisAt);
        if (!age.isEmpty()) parts << age;
        m_metaText = parts.join(QStringLiteral(" · "));
    }

    for (const editing::ParticipantCandidate& c : st.candidates) {
        Row r;
        r.id = c.id;
        r.personId = c.personId;
        r.name = c.name;
        r.checked = c.checked;
        r.rawIds = c.rawIds;
        r.sides = c.sides;
        if (!c.manual) ++m_candidateCount;
        for (const Row& old : previous)
            if (old.id == r.id) r.checked = old.checked;
        m_rows.append(r);
    }
    emit changed();
}

int ParticipantsViewModel::checkedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row& r) { return r.checked && !r.name.isEmpty(); }));
}

QString ParticipantsViewModel::participantIdOf(const QString& name) const
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    for (const Row& r : m_rows)
        if (r.name.compare(n, Qt::CaseInsensitive) == 0) return r.id;
    return {};
}

int ParticipantsViewModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id) return i;
    return -1;
}

bool ParticipantsViewModel::approve()
{
    if (!demo() && m_meetingId.isEmpty()) return false;
    QVector<editing::ApprovedRow> accepted;
    for (const Row& r : std::as_const(m_rows)) {
        if (!r.checked || r.name.isEmpty()) continue;
        accepted.append({r.id, r.personId, r.name, r.rawIds, r.sides});
    }
    const editing::ApprovalOutcome outcome = m_facade->approve(m_meetingId, accepted);
    if (!outcome.ok) return false;
    // Learning question: once, for the approved names.
    setLearn(outcome.questions);
    return true;
}

void ParticipantsViewModel::setLearn(const QVector<library::PersonTagQuestion>& questions)
{
    m_learn = questions;
    m_learnPairs = taglearning::pairs(questions);
    emit learnChanged();
}

QVariantMap ParticipantsViewModel::learnQuestion() const
{
    if (m_learn.isEmpty()) return {};
    const library::PersonTagQuestion& q = m_learn.first();
    return {{QStringLiteral("text"), taglearning::questionText(q)},
            {QStringLiteral("tagName"), q.tagName},
            {QStringLiteral("people"), q.names},
            {QStringLiteral("newPeople"), q.newPeople},
            {QStringLiteral("pairs"), m_learnPairs.size() > 1 ? taglearning::pairMaps(m_learnPairs) : QVariantList()}};
}

QString ParticipantsViewModel::answer(const library::PersonTagQuestion& q, bool yes)
{
    m_facade->answerLearning(q, yes);
    return taglearning::answeredText(q, yes);
}

QString ParticipantsViewModel::answerLearn(bool yes)
{
    if (m_learn.isEmpty()) return {};
    const library::PersonTagQuestion q = m_learn.first();
    setLearn({});
    return answer(q, yes);
}

QString ParticipantsViewModel::answerLearnPair(int index, bool yes)
{
    if (index < 0 || index >= m_learnPairs.size()) return {};
    const library::PersonTagQuestion pair = m_learnPairs.at(index);
    // The pair leaves the question (and the empty group too).
    QVector<library::PersonTagQuestion> rest = m_learn;
    for (library::PersonTagQuestion& q : rest)
        if (q.tagId == pair.tagId && q.newPeople == pair.newPeople) q.people.removeAll(pair.people.value(0));
    rest.removeIf([](const library::PersonTagQuestion& q) { return q.people.isEmpty(); });
    setLearn(rest);
    return answer(pair, yes);
}

void ParticipantsViewModel::dismissLearn()
{
    if (!m_learn.isEmpty()) setLearn({});
}

bool ParticipantsViewModel::skip()
{
    return (demo() || !m_meetingId.isEmpty()) && m_facade->skip(m_meetingId);
}

tanara::PersonChoice ParticipantsViewModel::choiceFor(const QString& personIdOrName) const
{
    const QString text = personIdOrName.trimmed();
    if (text.isEmpty()) return {};
    const QVector<editing::PersonInfo> known = m_facade->peopleDirectory();
    for (const editing::PersonInfo& p : known)
        if (!p.personId.isNull() && p.personId.toString() == text) return PersonChoice::existing(p.personId);
    for (const editing::PersonInfo& p : known)
        if (!p.personId.isNull() && persons::sameName(p.name, text)) return PersonChoice::existing(p.personId);
    return PersonChoice::create(text);
}

bool ParticipantsViewModel::nameParticipant(const QString& id, const QString& personIdOrName)
{
    const PersonChoice choice = choiceFor(personIdOrName);
    if (choice.isAnonymous() || id.isEmpty() || (!demo() && m_meetingId.isEmpty())) return false;
    const bool ok = m_facade->nameParticipant(m_meetingId, id, choice);
    if (ok && demo()) load();
    return ok;
}

bool ParticipantsViewModel::mergeParticipantInto(const QString& fromId, const QString& intoId)
{
    if (fromId.isEmpty() || intoId.isEmpty() || (!demo() && m_meetingId.isEmpty())) return false;
    const bool ok = m_facade->mergeParticipantInto(m_meetingId, fromId, intoId);
    if (ok && demo()) load();
    return ok;
}

bool ParticipantsViewModel::confirmParticipant(const QString& id, bool confirmed)
{
    if (id.isEmpty() || (!demo() && m_meetingId.isEmpty())) return false;
    const bool ok = m_facade->confirmParticipant(m_meetingId, id, confirmed);
    // The working copy follows (the reload after participantsChanged would keep the old check).
    if (ok)
        if (const int i = indexOf(id); i >= 0) m_rows[i].checked = confirmed;
    if (ok && demo()) load();
    else if (ok) emit changed();
    return ok;
}

QVariantMap ParticipantsViewModel::setSelfParticipant(const QString& id)
{
    QString why;
    const bool ok = !id.isEmpty() && (demo() || !m_meetingId.isEmpty())
                    && m_facade->setSelfParticipant(m_meetingId, id, &why);
    if (ok && demo()) load();
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("reason"), ok ? QString() : why}};
}

bool ParticipantsViewModel::unbind(const QString& participantId)
{
    if (demo()) {
        const int i = indexOf(participantId);
        if (i < 0) return false;
        m_rows[i].checked = false;
        m_rows[i].rawIds.clear();
        emit changed();
        return true;
    }
    if (m_meetingId.isEmpty()) return false;
    // Removed from the working copy too (the reload after participantsChanged would keep it).
    if (const int i = indexOf(participantId); i >= 0) m_rows[i].checked = false;
    return m_facade->unbind(m_meetingId, participantId);
}

} // namespace tanara_qml
