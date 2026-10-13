#include "ParticipantsViewModel.h"

#include "DemoText.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "TagLearning.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/tags/TagService.h"
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
    connect(ctx, &AppContext::controllerChanged, this, [this]() { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &ParticipantsViewModel::reload);
    connectController();
    reload();
}

app::AppController* ParticipantsViewModel::app() const { return jobsupport::resolveController(m_injected); }

QObject* ParticipantsViewModel::controllerObject() const { return app(); }

void ParticipantsViewModel::setController(QObject* controller)
{
    if (m_injected == controller) return;
    m_injected = controller;
    connectController();
    emit controllerChanged();
    reload();
}

bool ParticipantsViewModel::demo() const
{
    return !m_demoState.isEmpty() || jobsupport::demoMode(app());
}

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
    emit demoStateChanged();
    reload();
}

void ParticipantsViewModel::connectController()
{
    app::AppController* c = app();
    if (c == m_connected) return;
    for (const auto& conn : std::as_const(m_connections)) disconnect(conn);
    m_connections.clear();
    m_connected = c;
    if (!c) return;
    // End of the analysis / change of the meeting's participants: the groups are recomputed. The
    // user's checks stay (the working copy is restored by id).
    auto refreshFor = [this](const QString& meetingId) {
        if (meetingId != m_meetingId || demo()) return;
        load();
    };
    m_connections << connect(c, &app::AppController::participantsChanged, this, refreshFor);
    m_connections << connect(c, &app::AppController::participantAnalysisStarted, this, refreshFor);
    m_connections << connect(c, &app::AppController::participantAnalysisFinished, this, refreshFor);
}

void ParticipantsViewModel::reload()
{
    m_rows.clear();
    m_addedSeq = 0;
    if (demo()) loadDemo();
    else load();
}

void ParticipantsViewModel::load()
{
    app::AppController* c = app();
    // The working copy: the current checks / additions / names are kept by id.
    const QVector<Row> previous = m_rows;

    m_rows.clear();
    m_running = false;
    m_hasTranscript = false;
    m_pending = false;
    m_metaText.clear();
    m_rawLabels.clear();
    m_tagIds.clear();
    m_demoInfo.clear();
    m_selfName.clear();

    const Meeting m = (c && !m_meetingId.isEmpty()) ? c->store()->load(m_meetingId) : Meeting();
    if (m.id.isEmpty()) {
        reloadTagSuggestions();
        emit changed();
        return;
    }
    m_selfName = c->settings()->settings().userSpeakerName.trimmed();
    m_running = c->participantAnalysisRunning(m.id);
    m_pending = c->participantApprovalPending(m.id);
    m_hasTranscript = m.hasTranscript;
    m_tagIds = m.tagIds;

    const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
    if (!file.isEmpty()) {
        QStringList models;
        for (const QString& id : file.modelIds) models << shortModelName(id);
        QStringList parts{tr("hanglenyomat-elemzés")};
        if (!models.isEmpty()) parts << models.join(QStringLiteral(" + "));
        const QString age = ageText(QDateTime::fromString(file.at, Qt::ISODate));
        if (!age.isEmpty()) parts << age;
        m_metaText = parts.join(QStringLiteral(" · "));
    }

    if (m_hasTranscript) {
        for (const TranscriptLine& l : editing::speakeredit::loadTranscriptLines(m.folder))
            if (!l.rawLabel.isEmpty() && !m_rawLabels.contains(l.rawLabel)) m_rawLabels << l.rawLabel;
    }

    // If there was a (non-skipped) decision -> that check; otherwise the default.
    const bool decided = m.approval && !m.approval->skipped && !m.approval->solo;
    int color = 0;
    for (const Participant& p : m.participants) {
        Row r;
        r.id = p.id;
        r.name = p.personName;
        r.group = groupOf(processing::participants::participantGroup(p));
        // An invited but not heard candidate is unchecked by default: there is nobody to bind the name to.
        r.checked = decided ? p.approved : (r.group != 2 && processing::participants::defaultChecked(p));
        r.evidence = p.evidence;
        r.rawIds = p.rawSpeakerIds;
        r.sides = p.sides;
        r.heard = !p.sides.isEmpty();
        r.manual = p.source == ParticipantSource::Manual;
        r.colorIndex = color++;
        for (const Row& old : previous)
            if (old.id == r.id && !old.added) {
                r.checked = old.checked;
                if (r.name.isEmpty()) r.name = old.name;   // named in the dialog
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
    emit changed();
}

void ParticipantsViewModel::loadDemo()
{
    const QString s = m_demoState.isEmpty() ? QStringLiteral("doubts") : m_demoState;
    m_running = s == QLatin1String("running");
    m_hasTranscript = s != QLatin1String("noTranscriptYet") && !m_running;
    m_pending = !m_running;
    // The model name is of the models really used (without a controller: the recommended default).
    QStringList models;
    const app::AppController* c = app();
    for (const QString& id : c && !c->activeVoiceModelIds().isEmpty() ? c->activeVoiceModelIds()
                                                                     : processing::VoiceModelRegistry::defaultEnabledIds())
        models << shortModelName(id);
    m_metaText = tr("hanglenyomat-elemzés") + QStringLiteral(" · ") + models.join(QStringLiteral(" + "))
               + QStringLiteral(" · ") + tr("%n perce", "", 3);
    m_selfName = demoName("Kovács Lilla");
    m_tagIds = {QStringLiteral("demo-nordvik")};
    m_rawLabels.clear();
    m_demoInfo.clear();
    m_demoPeople = {
        {demoName("Szabó Bence"), true, 2, 11, {}, {}},
        {demoName("Szabó Dóra"), false, 0, 2, {}, {}},
        {demoName("Nagy Péter"), true, 1, 6, {}, {}},
        {demoName("Kiss Anna"), true, 3, 9, {}, {}},
    };

    const auto S = Polarity::Support, N = Polarity::Neutral, X = Polarity::Contradict;
    auto row = [this](const QString& id, const QString& name, int group, bool checked,
                      QVector<Evidence> evs, const QStringList& raw, const QStringList& sides) {
        Row r;
        r.id = id;
        r.name = name;
        r.group = group;
        r.checked = checked;
        r.evidence = std::move(evs);
        r.rawIds = raw;
        r.sides = sides;
        r.heard = !sides.isEmpty();
        r.colorIndex = int(m_rows.size());
        m_rows.append(r);
    };
    const QString mic = processing::participants::kSideMic, loop = processing::participants::kSideLoopback;
    if (m_running) {
        // During the analysis only participants added manually (on the Overview) show.
        row("m-lilla", demoName("Kovács Lilla"), 1, true, {ev(EvidenceKind::Manual, S, tr("kézzel felvéve"))}, {}, {});
        m_rows.last().manual = true;
        reloadTagSuggestions();
        emit changed();
        return;
    }

    row("v-lilla", demoName("Kovács Lilla"), 0, true,
        {ev(EvidenceKind::Side, S, demoText("mikrofon", "microphone")), ev(EvidenceKind::Voice, S, demoText("hang 96%", "voice 96%"))},
        {demoRaw(QStringLiteral("Beszélő 1@mic"))}, {mic});
    row("v-gabor", demoName("Fehér Gábor"), 0, true,
        {ev(EvidenceKind::Voice, S, demoText("hang 84%", "voice 84%")), ev(EvidenceKind::Calendar, S, demoText("naptár", "calendar")),
         ev(EvidenceKind::Tag, S, "címke: Nordvik")},
        {demoRaw(QStringLiteral("Beszélő 1@loopback"))}, {loop});
    row("v-arpad", demoName("Varga Árpád"), 0, true,
        {ev(EvidenceKind::Voice, S, demoText("hang 81%", "voice 81%")), ev(EvidenceKind::Calendar, S, demoText("naptár", "calendar"))},
        {demoRaw(QStringLiteral("Beszélő 3"))}, {loop});
    if (s != QLatin1String("allSure")) {
        row("v-eszter", demoName("Molnár Eszter"), 1, true,
            {ev(EvidenceKind::Voice, S, demoText("hang 61%", "voice 61%")), ev(EvidenceKind::Calendar, S, demoText("naptár", "calendar"))},
            {demoRaw(QStringLiteral("Beszélő 2"))}, {loop});
        row("v-bence", demoName("Tóth Bence"), 1, false,
            {ev(EvidenceKind::Voice, S, demoText("hang 58%", "voice 58%")), ev(EvidenceKind::Calendar, X, demoText("nincs a meghívottak közt", "not on the invite list")),
             ev(EvidenceKind::Tag, X, demoText("más címkéken szokott lenni", "usually on other tags"))},
            {demoRaw(QStringLiteral("Beszélő 2"))}, {loop});
    }
    if (s == QLatin1String("invitedNotHeard") || s == QLatin1String("addPerson")
        || s == QLatin1String("noTranscriptYet")) {
        row("c-reka", demoName("Lantos Réka"), 2, false,
            {ev(EvidenceKind::Calendar, N, demoText("naptár · elfogadta", "calendar · accepted")), ev(EvidenceKind::Voice, X, demoText("nincs hanglenyomata", "has no voiceprint"))},
            {}, {});
    }
    m_rawLabels = QStringList{demoRaw(QStringLiteral("Beszélő 1")), demoRaw(QStringLiteral("Beszélő 2")),
                              demoRaw(QStringLiteral("Beszélő 3")), demoRaw(QStringLiteral("Beszélő 4"))};
    if (s == QLatin1String("allSure")) m_rawLabels = {demoRaw(QStringLiteral("Beszélő 1")), demoRaw(QStringLiteral("Beszélő 3"))};
    if (!m_hasTranscript) {
        m_rawLabels.clear();
        for (Row& r : m_rows) r.rawIds.clear();
    }
    if (s == QLatin1String("addPerson")) {
        reloadTagSuggestions();
        acceptTagSuggestion(0);       // "+ Alex Smith" based on #Nordvik
        addPerson(demoName("Kiss Anna"));
        return;
    }
    reloadTagSuggestions();
    emit changed();
}

// A fictional sample of the question after approval (screenshot, demo).
void ParticipantsViewModel::loadDemoLearn()
{
    if (m_demoState == QLatin1String("learnNewPerson")) {
        setLearn({library::PersonTagQuestion{QStringLiteral("demo-nordvik"), QStringLiteral("Nordvik"),
                                    {demoName("Szabó Bence")}, true}});
        return;
    }
    setLearn({library::PersonTagQuestion{QStringLiteral("demo-nordvik"), QStringLiteral("Nordvik"),
                                {demoName("Fehér Gábor"), demoName("Varga Árpád")}, false},
              library::PersonTagQuestion{QStringLiteral("demo-partnerek"), demoText("Partnerek", "Partners"),
                                {demoName("Fehér Gábor")}, false}});
}

void ParticipantsViewModel::reloadTagSuggestions()
{
    m_tagLabel.clear();
    m_tagSuggestions.clear();
    QStringList names;
    for (const Row& r : std::as_const(m_rows))
        if (!r.name.isEmpty()) names << r.name;
    if (!m_selfName.isEmpty()) names << m_selfName;

    if (demo()) {
        if (m_running || indexOfName(demoName("Szabó Bence")) >= 0) return;
        m_tagLabel = tr("A #%1 alapján:").arg(QStringLiteral("Nordvik"));
        m_tagSuggestions.append({demoName("Szabó Bence"), QStringLiteral("Nordvik"), 11, 14});
        return;
    }
    app::AppController* c = app();
    library::TagService* tags = c ? c->tags() : nullptr;
    if (!tags) return;
    // The first tag that has a suggestion; at most 3 people.
    for (const QString& tagId : std::as_const(m_tagIds)) {
        const QVector<library::PersonTagStat> stats = tags->suggestPeopleForTag(tagId, names, 3);
        if (stats.isEmpty()) continue;
        const QString tagName = tags->tag(tagId).name;
        m_tagLabel = tr("A #%1 alapján:").arg(tagName);
        for (const library::PersonTagStat& st : stats)
            m_tagSuggestions.append({st.name, tagName, st.shared, st.tagTotal});
        break;
    }
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
        QString text = e.text;
        // The design's short forms: "#Nordvik", "mic track".
        if (e.kind == EvidenceKind::Tag && text.startsWith(QStringLiteral("címke: "))) {
            text = QLatin1Char('#') + text.mid(7);
            const int more = int(e.detail.split(QStringLiteral(", "), Qt::SkipEmptyParts).size()) - 1;
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
            {QStringLiteral("mappingDoubt"), doubt}};
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

QVector<editing::PersonInfo> ParticipantsViewModel::people() const
{
    if (demo()) return m_demoPeople;
    app::AppController* c = app();
    return c ? c->peopleDirectory() : QVector<editing::PersonInfo>();
}

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
    if (demo()) {
        if (m_demoState.startsWith(QLatin1String("learn"))) loadDemoLearn();
        return true;
    }
    app::AppController* c = app();
    if (!c || m_meetingId.isEmpty()) return false;
    QVector<Participant> accepted;
    for (const Row& r : std::as_const(m_rows)) {
        if (!r.checked || r.name.isEmpty()) continue;
        Participant p;
        p.id = r.added ? QString() : r.id;
        p.personName = r.name;
        p.rawSpeakerIds = r.rawIds;
        p.sides = r.sides;
        accepted.append(p);
    }
    if (!c->approveParticipants(m_meetingId, accepted)) return false;
    // Learning question: once, for the approved names.
    QVector<library::PersonTagQuestion> questions;
    if (library::TagService* tags = c->tags()) {
        QStringList names;
        for (const Participant& p : std::as_const(accepted)) names << p.personName;
        questions = tags->learningQuestions(m_meetingId, names);
        tags->markLearningAsked(m_meetingId, questions);
    }
    setLearn(questions);
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
            {QStringLiteral("people"), q.people},
            {QStringLiteral("newPeople"), q.newPeople},
            {QStringLiteral("pairs"), m_learnPairs.size() > 1 ? taglearning::pairMaps(m_learnPairs) : QVariantList()}};
}

QString ParticipantsViewModel::answer(const library::PersonTagQuestion& q, bool yes)
{
    if (!demo())
        if (app::AppController* c = app(); c && c->tags()) c->tags()->answerLearning(q, yes);
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
    if (demo()) return true;
    app::AppController* c = app();
    return c && !m_meetingId.isEmpty() && c->skipApproval(m_meetingId);
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
    app::AppController* c = app();
    if (!c || m_meetingId.isEmpty()) return false;
    // Removed from the working copy too (the reload after participantsChanged would keep it).
    if (const int i = indexOf(participantId); i >= 0) m_rows[i].checked = false;
    return c->unbindParticipant(m_meetingId, participantId);
}

} // namespace tanara_qml
