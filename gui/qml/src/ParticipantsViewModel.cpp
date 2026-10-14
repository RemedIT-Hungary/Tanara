#include "ParticipantsViewModel.h"

#include "DemoText.h"

#include "AppContext.h"
#include "TagLearning.h"

#include "tanara/domain/EvidenceText.h"
#include "tanara/editing/participants/IParticipantsFacade.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

namespace {

QString kindKey(EvidenceKind k)
{
    switch (k) {
    case EvidenceKind::Voice: return QStringLiteral("voice");
    case EvidenceKind::Side: return QStringLiteral("side");
    case EvidenceKind::Tag: return QStringLiteral("tag");
    case EvidenceKind::LineCount: return QStringLiteral("lines");
    case EvidenceKind::Similarity: return QStringLiteral("similarity");
    case EvidenceKind::Calendar: return QStringLiteral("calendar");
    case EvidenceKind::Manual: return QStringLiteral("manual");
    }
    return QStringLiteral("voice");
}

QString polarityKey(Polarity p)
{
    switch (p) {
    case Polarity::Support: return QStringLiteral("support");
    case Polarity::Contradict: return QStringLiteral("contradict");
    case Polarity::Neutral: return QStringLiteral("neutral");
    }
    return QStringLiteral("neutral");
}

int groupOf(ParticipantGroup g)
{
    switch (g) {
    case ParticipantGroup::Sure: return 0;
    case ParticipantGroup::Doubt: return 1;
    case ParticipantGroup::InvitedNotHeard: return 2;
    }
    return 1;
}

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

Evidence ev(EvidenceKind k, Polarity p, const QString& text)
{
    Evidence e;
    e.kind = k;
    e.polarity = p;
    e.text = text;
    return e;
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
    m_addedSeq = 0;
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
    m_metaText.clear();
    m_rawLabels.clear();
    m_tagIds.clear();
    m_selfName.clear();

    const editing::ParticipantsState st = m_facade->state(m_meetingId);
    if (!st.found) {
        reloadTagSuggestions();
        emit changed();
        return;
    }
    m_selfName = st.selfName;
    m_running = st.running;
    m_pending = st.approvalPending;
    m_hasTranscript = st.hasTranscript;
    m_tagIds = st.tagIds;
    m_rawLabels = st.rawLabels;

    if (!st.analysisModelIds.isEmpty() || st.analysisAt.isValid()) {
        QStringList models;
        for (const QString& id : st.analysisModelIds) models << shortModelName(id);
        QStringList parts{tr("hanglenyomat-elemzés")};
        if (!models.isEmpty()) parts << models.join(QStringLiteral(" + "));
        const QString age = ageText(st.analysisAt);
        if (!age.isEmpty()) parts << age;
        m_metaText = parts.join(QStringLiteral(" · "));
    }

    int color = 0;
    for (const editing::ParticipantCandidate& c : st.candidates) {
        Row r;
        r.id = c.id;
        r.personId = c.personId;
        r.name = c.name;
        r.group = groupOf(c.group);
        r.checked = c.checked;
        r.evidence = c.evidence;
        r.rawIds = c.rawIds;
        r.sides = c.sides;
        r.sample = c.sample;
        r.heard = !c.sides.isEmpty();
        r.manual = c.manual;
        r.colorIndex = color++;
        for (const Row& old : previous)
            if (old.id == r.id && !old.added) {
                r.checked = old.checked;
                if (r.name.isEmpty()) {   // named in the dialog
                    r.name = old.name;
                    r.personId = old.personId;
                }
            }
        m_rows.append(r);
    }
    for (const Row& old : previous)
        if (old.added && indexOfName(old.name) < 0) {
            Row r = old;
            r.colorIndex = color++;
            m_rows.append(r);
        }

    reloadTagSuggestions();
    // The design state "addPerson" (screenshot): the tag suggestion accepted and a name added.
    if (demo() && m_demoState == QLatin1String("addPerson")) {
        acceptTagSuggestion(0);       // "+ Alex Smith" based on #Nordvik
        addPerson(demoName("Kiss Anna"));
        return;
    }
    emit changed();
}

void ParticipantsViewModel::reloadTagSuggestions()
{
    m_tagLabel.clear();
    m_tagSuggestions.clear();
    // The people already in the dialog (a typed name: resolved by name) and, in the facade, the own person.
    PersonIds presentIds;
    QStringList presentNames;
    for (const Row& r : std::as_const(m_rows)) {
        if (!r.personId.isNull()) presentIds << r.personId;
        else if (!r.name.isEmpty()) presentNames << r.name;
    }
    // The first tag that has a suggestion; at most 3 people.
    const editing::TagPeopleHint hint = m_facade->suggestPeople(m_tagIds, presentIds, presentNames, 3);
    if (hint.people.isEmpty()) return;
    m_tagLabel = tr("A #%1 alapján:").arg(hint.tagName);
    for (const library::PersonTagStat& st : hint.people)
        m_tagSuggestions.append({st.name, hint.tagName, st.shared, st.tagTotal, st.personId});
}

QVariantList ParticipantsViewModel::tagSuggestions() const
{
    QVariantList out;
    for (const TagSuggestion& t : m_tagSuggestions)
        out << QVariantMap{{QStringLiteral("name"), t.name},
                           {QStringLiteral("countText"),
                            tr("%1 / %2 megbeszélésen").arg(t.shared).arg(t.total)}};
    return out;
}

QVariantMap ParticipantsViewModel::rowMap(const Row& r) const
{
    QVariantList evs;
    const QString side = r.sides.size() == 1 ? r.sides.first() : QString();
    for (const Evidence& e : r.evidence) {
        QString text = evidenceText(e);
        // The design's short forms: "#Nordvik", "mic track".
        if (e.kind == EvidenceKind::Tag && text.startsWith(QStringLiteral("címke: "))) {
            text = QLatin1Char('#') + text.mid(7);
            const int more = int(evidenceDetail(e).split(QStringLiteral(", "), Qt::SkipEmptyParts).size()) - 1;
            if (more > 0) text += QStringLiteral(" +%1").arg(more);
        } else if (e.kind == EvidenceKind::Side && e.polarity != Polarity::Contradict && !side.isEmpty()) {
            text = processing::participants::sideLabel(side) + tr("-sáv");
        }
        evs << QVariantMap{{QStringLiteral("kind"), kindKey(e.kind)},
                           {QStringLiteral("polarity"), polarityKey(e.polarity)},
                           {QStringLiteral("text"), text},
                           {QStringLiteral("side"), e.kind == EvidenceKind::Side ? side : QString()}};
    }

    QString mapping;
    bool pending = false, doubt = false;
    if (!m_hasTranscript) {
        pending = r.heard;
        if (pending) mapping = tr("átirat után");
    } else {
        QStringList shown;
        for (const QString& raw : r.rawIds) {
            shown << processing::participants::rawDisplay(raw);
            if (!r.checked && rawBoundToChecked(processing::participants::rawLabelOf(raw), r.id)) doubt = true;
        }
        mapping = shown.join(QStringLiteral(", "));
        if (doubt) mapping += QStringLiteral(" ?");
    }

    const bool self = !m_selfName.isEmpty() && r.name.compare(m_selfName, Qt::CaseInsensitive) == 0;
    return {{QStringLiteral("id"), r.id},
            {QStringLiteral("name"), r.name},
            {QStringLiteral("displayName"), r.name.isEmpty() ? tr("Ismeretlen hang")
                                            : self ? tr("%1 (te)").arg(r.name) : r.name},
            {QStringLiteral("colorIndex"), r.colorIndex},
            {QStringLiteral("checked"), r.checked},
            {QStringLiteral("checkable"), !r.name.isEmpty()},
            {QStringLiteral("anonymous"), r.name.isEmpty()},
            {QStringLiteral("added"), r.added},
            {QStringLiteral("evidence"), evs},
            {QStringLiteral("mapping"), mapping},
            {QStringLiteral("mappingPending"), pending},
            {QStringLiteral("mappingDoubt"), doubt},
            {QStringLiteral("sampleTrackId"), r.sample.isValid() ? r.sample.trackId : QString()},
            {QStringLiteral("sampleStartMs"), r.sample.isValid() ? r.sample.startMs : qint64(-1)},
            {QStringLiteral("sampleEndMs"), r.sample.isValid() ? r.sample.endMs : qint64(-1)}};
}

QVariantList ParticipantsViewModel::groups() const
{
    static const char* labels[] = {QT_TR_NOOP("BIZTOS"), QT_TR_NOOP("KÉTSÉGES"),
                                   QT_TR_NOOP("MEGHÍVOTT, DE NEM HALLOTTUK"), QT_TR_NOOP("KÉZZEL FELVÉVE")};
    static const char* hints[] = {QT_TR_NOOP("a hang mellett még egy forrás egyezik"),
                                  QT_TR_NOOP("amit valami cáfol, alapból nincs bejelölve"), "",
                                  QT_TR_NOOP("a hangját még nem hallottuk")};
    static const char* keys[] = {"sure", "doubt", "invited", "manual"};
    QVariantList out;
    for (int g = 0; g < 4; ++g) {
        QVariantList rows;
        for (const Row& r : m_rows)
            if (((r.manual && !r.heard) ? 3 : r.group) == g) rows << rowMap(r);
        if (rows.isEmpty()) continue;
        out << QVariantMap{{QStringLiteral("key"), QString::fromLatin1(keys[g])},
                           {QStringLiteral("label"), tr(labels[g])},
                           {QStringLiteral("hint"), hints[g][0] ? tr(hints[g]) : QString()},
                           {QStringLiteral("count"), int(rows.size())},
                           {QStringLiteral("rows"), rows}};
    }
    return out;
}

int ParticipantsViewModel::checkedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row& r) { return r.checked && !r.name.isEmpty(); }));
}

bool ParticipantsViewModel::rawBoundToChecked(const QString& rawLabel, const QString& exceptId) const
{
    for (const Row& r : m_rows) {
        if (r.id == exceptId || !r.checked || r.name.isEmpty()) continue;
        for (const QString& raw : r.rawIds)
            if (processing::participants::rawLabelOf(raw) == rawLabel) return true;
    }
    return false;
}

QString ParticipantsViewModel::infoText() const
{
    if (m_running)
        return tr("A hanglenyomat-elemzés fut; a jelöltek pár másodperc múlva itt lesznek.");
    if (!m_hasTranscript)
        return tr("Az átirat ebből köti a beszélőket, amikor megérkezik; a kimaradók névtelenek maradnak.");
    QStringList unbound;
    for (const QString& label : m_rawLabels)
        if (!rawBoundToChecked(label, QString())) unbound << label;
    if (unbound.isEmpty())
        return m_rawLabels.isEmpty() ? QString() : tr("Minden nyers beszélő résztvevőhöz kötődik.");
    if (unbound.size() == 1) {
        static const QRegularExpression num(QStringLiteral("(\\d+)$"));
        const auto mt = num.match(unbound.first());
        if (mt.hasMatch())
            return tr("A %1. nyers beszélő senkihez sincs kötve, ezért névtelen marad.").arg(mt.captured(1));
        return tr("„%1” senkihez sincs kötve, ezért névtelen marad.").arg(unbound.first());
    }
    return tr("%n nyers beszélő senkihez sincs kötve (%1), ezért névtelenek maradnak.", "",
              int(unbound.size())).arg(unbound.join(QStringLiteral(", ")));
}

QVector<editing::PersonInfo> ParticipantsViewModel::people() const { return m_facade->dialogPeople(); }

bool ParticipantsViewModel::isMeetingPerson(const QString& name) const { return indexOfName(name) >= 0; }

int ParticipantsViewModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id) return i;
    return -1;
}

int ParticipantsViewModel::indexOfName(const QString& name) const
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).name.compare(n, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QString ParticipantsViewModel::nextAddedId() { return QStringLiteral("new:%1").arg(++m_addedSeq); }

void ParticipantsViewModel::setChecked(const QString& id, bool checked)
{
    const int i = indexOf(id);
    if (i < 0 || m_rows.at(i).name.isEmpty() || m_rows.at(i).checked == checked) return;
    m_rows[i].checked = checked;
    emit changed();
}

QString ParticipantsViewModel::addPerson(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    if (const int i = indexOfName(n); i >= 0) {
        if (!m_rows.at(i).checked) {
            m_rows[i].checked = true;
            emit changed();
        }
        return m_rows.at(i).id;
    }
    Row r;
    r.id = nextAddedId();
    r.name = n;
    r.added = true;
    r.manual = true;
    r.checked = true;
    r.group = 1;
    r.colorIndex = int(m_rows.size());
    r.evidence = {ev(EvidenceKind::Manual, Polarity::Support, tr("kézzel felvéve"))};
    m_rows.append(r);
    reloadTagSuggestions();
    emit changed();
    return r.id;
}

void ParticipantsViewModel::acceptTagSuggestion(int index)
{
    if (index < 0 || index >= m_tagSuggestions.size()) return;
    const TagSuggestion t = m_tagSuggestions.at(index);
    const QString id = addPerson(t.name);
    if (const int i = indexOf(id); i >= 0 && m_rows.at(i).added) {
        m_rows[i].personId = t.personId;
        m_rows[i].evidence = {ev(EvidenceKind::Tag, Polarity::Support,
                                 QStringLiteral("címke: %1").arg(t.tagName))};
        emit changed();
    }
}

void ParticipantsViewModel::nameRow(const QString& id, const QString& name)
{
    const int i = indexOf(id);
    const QString n = name.trimmed();
    if (i < 0 || n.isEmpty()) return;
    // A row with this name is already there: we check it, this one is not renamed (one name, one participant).
    if (const int j = indexOfName(n); j >= 0 && j != i) {
        m_rows[j].checked = true;
        emit changed();
        return;
    }
    m_rows[i].name = n;
    m_rows[i].personId = PersonId();   // resolved by name on approval
    m_rows[i].checked = true;
    reloadTagSuggestions();
    emit changed();
}

void ParticipantsViewModel::removeAdded(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0 || !m_rows.at(i).added) return;
    m_rows.removeAt(i);
    reloadTagSuggestions();
    emit changed();
}

bool ParticipantsViewModel::approve()
{
    if (!demo() && m_meetingId.isEmpty()) return false;
    QVector<editing::ApprovedRow> accepted;
    for (const Row& r : std::as_const(m_rows)) {
        if (!r.checked || r.name.isEmpty()) continue;
        accepted.append({r.added ? QString() : r.id, r.personId, r.name, r.rawIds, r.sides});
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
