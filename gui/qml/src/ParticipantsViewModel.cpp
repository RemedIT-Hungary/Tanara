#include "ParticipantsViewModel.h"

#include "AppContext.h"
#include "JobSupport.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/ParticipantAnalysis.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

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

// Rövid modellnév a meta-sorhoz: „CAM++ (3D-Speaker)" → „CAM++".
QString shortModelName(const QString& id)
{
    const auto spec = VoiceModelRegistry::spec(id);
    const QString name = spec ? spec->displayName : id;
    return name.section(QLatin1Char(' '), 0, 0);
}

// „3 perce" / „2 órája" / „tegnap" / „okt. 1." — a hangelemzés kora.
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

AppController* ParticipantsViewModel::app() const { return jobsupport::resolveController(m_injected); }

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
    AppController* c = app();
    if (c == m_connected) return;
    for (const auto& conn : std::as_const(m_connections)) disconnect(conn);
    m_connections.clear();
    m_connected = c;
    if (!c) return;
    // Az elemzés vége / a meeting résztvevőinek változása: a csoportok újraszámolása. A
    // felhasználó jelölései megmaradnak (a munkapéldány id szerint visszaáll).
    auto refreshFor = [this](const QString& meetingId) {
        if (meetingId != m_meetingId || demo()) return;
        load();
    };
    m_connections << connect(c, &AppController::participantsChanged, this, refreshFor);
    m_connections << connect(c, &AppController::participantAnalysisStarted, this, refreshFor);
    m_connections << connect(c, &AppController::participantAnalysisFinished, this, refreshFor);
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
    AppController* c = app();
    // A munkapéldány: a mostani jelölések / hozzáadások / elnevezések id szerint megmaradnak.
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

    const ParticipantAnalysisFile file = ParticipantAnalysisFile::load(m.folder);
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
        for (const TranscriptLine& l : speakeredit::loadTranscriptLines(m.folder))
            if (!l.rawLabel.isEmpty() && !m_rawLabels.contains(l.rawLabel)) m_rawLabels << l.rawLabel;
    }

    // Volt már (nem kihagyott) döntés → az akkori jelölés; különben az alapértelmezés.
    const bool decided = m.approval && !m.approval->skipped && !m.approval->solo;
    int color = 0;
    for (const Participant& p : m.participants) {
        Row r;
        r.id = p.id;
        r.name = p.personName;
        r.group = groupOf(participants::participantGroup(p));
        // A meghívott, de nem hallott jelölt alapból ki (V2): nincs kit a nevére kötni.
        r.checked = decided ? p.approved : (r.group != 2 && participants::defaultChecked(p));
        r.evidence = p.evidence;
        r.rawIds = p.rawSpeakerIds;
        r.sides = p.sides;
        r.heard = !p.sides.isEmpty();
        r.manual = p.source == ParticipantSource::Manual;
        r.colorIndex = color++;
        for (const Row& old : previous)
            if (old.id == r.id && !old.added) {
                r.checked = old.checked;
                if (r.name.isEmpty()) r.name = old.name;   // a párbeszédben elnevezett
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
    m_metaText = tr("hanglenyomat-elemzés") + QStringLiteral(" · CAM++ + WeSpeaker · ") + tr("%n perce", "", 3);
    m_selfName = QStringLiteral("Kovács Lilla");
    m_tagIds = {QStringLiteral("demo-nordvik")};
    m_rawLabels.clear();
    m_demoInfo.clear();
    m_demoPeople = {
        {QStringLiteral("Szabó Bence"), true, 2, 11, {}, {}},
        {QStringLiteral("Szabó Dóra"), false, 0, 2, {}, {}},
        {QStringLiteral("Nagy Péter"), true, 1, 6, {}, {}},
        {QStringLiteral("Kiss Anna"), true, 3, 9, {}, {}},
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
    const QString mic = participants::kSideMic, loop = participants::kSideLoopback;
    if (m_running) {
        // Elemzés közben csak a kézzel (az Áttekintésen) felvett résztvevők látszanak.
        row("m-lilla", "Kovács Lilla", 1, true, {ev(EvidenceKind::Manual, S, tr("kézzel felvéve"))}, {}, {});
        m_rows.last().manual = true;
        reloadTagSuggestions();
        emit changed();
        return;
    }

    row("v-lilla", "Kovács Lilla", 0, true,
        {ev(EvidenceKind::Side, S, "mikrofon"), ev(EvidenceKind::Voice, S, "hang 96%")},
        {"Beszélő 1@mic"}, {mic});
    row("v-gabor", "Fehér Gábor", 0, true,
        {ev(EvidenceKind::Voice, S, "hang 84%"), ev(EvidenceKind::Calendar, S, "naptár"),
         ev(EvidenceKind::Tag, S, "címke: Nordvik")},
        {"Beszélő 1@loopback"}, {loop});
    row("v-arpad", "Varga Árpád", 0, true,
        {ev(EvidenceKind::Voice, S, "hang 81%"), ev(EvidenceKind::Calendar, S, "naptár")},
        {"Beszélő 3"}, {loop});
    if (s != QLatin1String("allSure")) {
        row("v-eszter", "Molnár Eszter", 1, true,
            {ev(EvidenceKind::Voice, S, "hang 61%"), ev(EvidenceKind::Calendar, S, "naptár")},
            {"Beszélő 2"}, {loop});
        row("v-bence", "Tóth Bence", 1, false,
            {ev(EvidenceKind::Voice, S, "hang 58%"), ev(EvidenceKind::Calendar, X, "nincs a meghívottak közt"),
             ev(EvidenceKind::Tag, X, "más címkéken szokott lenni")},
            {"Beszélő 2"}, {loop});
    }
    if (s == QLatin1String("invitedNotHeard") || s == QLatin1String("addPerson")
        || s == QLatin1String("noTranscriptYet")) {
        row("c-reka", "Lantos Réka", 2, false,
            {ev(EvidenceKind::Calendar, N, "naptár · elfogadta"), ev(EvidenceKind::Voice, X, "nincs hanglenyomata")},
            {}, {});
    }
    m_rawLabels = {"Beszélő 1", "Beszélő 2", "Beszélő 3", "Beszélő 4"};
    if (s == QLatin1String("allSure")) m_rawLabels = {"Beszélő 1", "Beszélő 3"};
    if (!m_hasTranscript) {
        m_rawLabels.clear();
        for (Row& r : m_rows) r.rawIds.clear();
    }
    if (s == QLatin1String("addPerson")) {
        reloadTagSuggestions();
        acceptTagSuggestion(0);       // „+ Szabó Bence" a #Nordvik alapján
        addPerson(QStringLiteral("Kiss Anna"));
        return;
    }
    reloadTagSuggestions();
    emit changed();
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
        if (m_running || indexOfName(QStringLiteral("Szabó Bence")) >= 0) return;
        m_tagLabel = tr("A #%1 alapján:").arg(QStringLiteral("Nordvik"));
        m_tagSuggestions.append({QStringLiteral("Szabó Bence"), QStringLiteral("Nordvik"), 11, 14});
        return;
    }
    AppController* c = app();
    TagService* tags = c ? c->tags() : nullptr;
    if (!tags) return;
    // Az első címke, amelyre van javaslat; legfeljebb 3 személy.
    for (const QString& tagId : std::as_const(m_tagIds)) {
        const QVector<PersonTagStat> stats = tags->suggestPeopleForTag(tagId, names, 3);
        if (stats.isEmpty()) continue;
        const QString tagName = tags->tag(tagId).name;
        m_tagLabel = tr("A #%1 alapján:").arg(tagName);
        for (const PersonTagStat& st : stats)
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
        // A design rövid formái: „#Nordvik", „mikrofon-sáv".
        if (e.kind == EvidenceKind::Tag && text.startsWith(QStringLiteral("címke: "))) {
            text = QLatin1Char('#') + text.mid(7);
            const int more = int(e.detail.split(QStringLiteral(", "), Qt::SkipEmptyParts).size()) - 1;
            if (more > 0) text += QStringLiteral(" +%1").arg(more);
        } else if (e.kind == EvidenceKind::Side && e.polarity != Polarity::Contradict && !side.isEmpty()) {
            text = participants::sideLabel(side) + tr("-sáv");
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
            shown << participants::rawDisplay(raw);
            if (!r.checked && rawBoundToChecked(participants::rawLabelOf(raw), r.id)) doubt = true;
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
            if (participants::rawLabelOf(raw) == rawLabel) return true;
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

QVector<PersonInfo> ParticipantsViewModel::people() const
{
    if (demo()) return m_demoPeople;
    AppController* c = app();
    return c ? c->peopleDirectory() : QVector<PersonInfo>();
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
    // Már szerepel ilyen nevű sor: azt jelöljük, ezt nem nevezzük át (egy név, egy résztvevő).
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
    if (demo()) return true;
    AppController* c = app();
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
    return c->approveParticipants(m_meetingId, accepted);
}

bool ParticipantsViewModel::skip()
{
    if (demo()) return true;
    AppController* c = app();
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
    AppController* c = app();
    if (!c || m_meetingId.isEmpty()) return false;
    // A munkapéldányban is kivesszük (a participantsChanged utáni újratöltés megtartaná).
    if (const int i = indexOf(participantId); i >= 0) m_rows[i].checked = false;
    return c->unbindParticipant(m_meetingId, participantId);
}

} // namespace tanara_qml
