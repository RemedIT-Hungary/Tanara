#include "tanara/store/JsonSerialization.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/summary/SummarySources.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QJsonValue>

namespace tanara {

namespace {

// Enum ⟷ string, hogy a JSON ember-olvasható és stabil maradjon.
QString trackKindToString(TrackKind k)
{
    switch (k) {
    case TrackKind::Mic:      return QStringLiteral("mic");
    case TrackKind::Loopback: return QStringLiteral("loopback");
    case TrackKind::Other:    return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

TrackKind trackKindFromString(const QString& s)
{
    if (s == QLatin1String("mic"))      return TrackKind::Mic;
    if (s == QLatin1String("loopback")) return TrackKind::Loopback;
    return TrackKind::Other;
}

QJsonArray stringListToArray(const QStringList& list)
{
    QJsonArray arr;
    for (const auto& s : list)
        arr.append(s);
    return arr;
}

QStringList arrayToStringList(const QJsonArray& arr)
{
    QStringList list;
    for (const auto& v : arr)
        list.append(v.toString());
    return list;
}

} // namespace

// ---- Track ----------------------------------------------------------------
QJsonObject toJson(const Track& t)
{
    QJsonObject o;
    o[QStringLiteral("id")]           = t.id;
    o[QStringLiteral("deviceName")]   = t.deviceName;
    o[QStringLiteral("file")]         = t.file;
    o[QStringLiteral("speakerLabel")] = t.speakerLabel;
    o[QStringLiteral("kind")]         = trackKindToString(t.kind);
    o[QStringLiteral("fixedSpeaker")] = t.fixedSpeaker;
    o[QStringLiteral("sampleRate")]   = t.sampleRate;
    o[QStringLiteral("channels")]     = t.channels;
    o[QStringLiteral("active")]       = t.active;
    o[QStringLiteral("peakLevel")]    = t.peakLevel;
    if (!t.customName.isEmpty())
        o[QStringLiteral("customName")] = t.customName;
    if (t.startOffsetMs != 0)
        o[QStringLiteral("startOffsetMs")] = double(t.startOffsetMs);
    if (t.speechRatio >= 0.0)
        o[QStringLiteral("speechRatio")] = t.speechRatio;
    if (!t.excludedReason.isEmpty())
        o[QStringLiteral("excludedReason")] = t.excludedReason;
    return o;
}

Track trackFromJson(const QJsonObject& o)
{
    Track t;
    t.id           = o.value(QStringLiteral("id")).toString();
    t.deviceName   = o.value(QStringLiteral("deviceName")).toString();
    t.file         = o.value(QStringLiteral("file")).toString();
    t.speakerLabel = o.value(QStringLiteral("speakerLabel")).toString();
    t.kind         = trackKindFromString(o.value(QStringLiteral("kind")).toString());
    t.fixedSpeaker = o.value(QStringLiteral("fixedSpeaker")).toBool();
    t.sampleRate   = o.value(QStringLiteral("sampleRate")).toInt(48000);
    t.channels     = o.value(QStringLiteral("channels")).toInt(1);
    t.active       = o.value(QStringLiteral("active")).toBool(true);   // régi felvétel → aktív
    t.peakLevel    = static_cast<float>(o.value(QStringLiteral("peakLevel")).toDouble(0.0));
    t.customName   = o.value(QStringLiteral("customName")).toString();
    t.startOffsetMs = qint64(o.value(QStringLiteral("startOffsetMs")).toDouble(0.0));
    t.speechRatio  = o.value(QStringLiteral("speechRatio")).toDouble(-1.0);
    t.excludedReason = o.value(QStringLiteral("excludedReason")).toString();
    // Régi meeting: a felvétel utáni csend-eldobás (active=false) ok nélkül → „nincs beszéd".
    if (!t.active && t.excludedReason.isEmpty())
        t.excludedReason = QStringLiteral("noSpeech");
    return t;
}

// ---- ActionItem -----------------------------------------------------------
QJsonObject toJson(const ActionItem& a)
{
    QJsonObject o;
    o[QStringLiteral("text")]  = a.text;
    o[QStringLiteral("owner")] = a.owner;
    o[QStringLiteral("due")]   = a.due;
    return o;
}

ActionItem actionItemFromJson(const QJsonObject& o)
{
    ActionItem a;
    a.text  = o.value(QStringLiteral("text")).toString();
    a.owner = o.value(QStringLiteral("owner")).toString();
    a.due   = o.value(QStringLiteral("due")).toString();
    return a;
}

// ---- Summary --------------------------------------------------------------
QJsonObject toJson(const Summary& s)
{
    QJsonObject o;
    o[QStringLiteral("execSummary")] = s.execSummary;
    o[QStringLiteral("decisions")]   = stringListToArray(s.decisions);

    QJsonArray items;
    for (const auto& a : s.actionItems)
        items.append(toJson(a));
    o[QStringLiteral("actionItems")] = items;

    o[QStringLiteral("participants")] = stringListToArray(s.participants);
    o[QStringLiteral("openQuestions")] = stringListToArray(s.openQuestions);

    // A memó szakaszai időrendben (az idő ms-ban; -1 = ismeretlen).
    QJsonArray memo;
    for (const MemoSection& m : s.memo) {
        QJsonObject mo;
        mo[QStringLiteral("title")]   = m.title;
        mo[QStringLiteral("startMs")] = static_cast<double>(m.startMs);
        mo[QStringLiteral("endMs")]   = static_cast<double>(m.endMs);
        mo[QStringLiteral("points")]  = stringListToArray(m.points);
        if (!m.speakers.isEmpty())
            mo[QStringLiteral("speakers")] = stringListToArray(m.speakers);
        memo.append(mo);
    }
    o[QStringLiteral("memo")] = memo;

    // Forrás-hivatkozások: az állítások (a staleBecause futásidejű, nem tároljuk) és a
    // hivatkozott megszólalások beszélője a készítéskor.
    if (!s.statements.isEmpty()) {
        QJsonArray sts;
        for (const SummaryStatement& st : s.statements) {
            QJsonObject so;
            so[QStringLiteral("id")]   = st.id;
            so[QStringLiteral("kind")] = summarysrc::kindToString(st.kind);
            so[QStringLiteral("text")] = st.text;
            if (!st.owner.isEmpty()) so[QStringLiteral("owner")] = st.owner;
            if (st.flagged) so[QStringLiteral("flagged")] = true;
            QJsonArray spans;
            for (const SourceSpan& sp : st.sourceSpans) {
                QJsonObject spo;
                spo[QStringLiteral("startMs")] = static_cast<double>(sp.startMs);
                spo[QStringLiteral("endMs")]   = static_cast<double>(sp.endMs);
                spo[QStringLiteral("utteranceIds")] = stringListToArray(sp.utteranceIds);
                spans.append(spo);
            }
            so[QStringLiteral("sourceSpans")] = spans;
            sts.append(so);
        }
        o[QStringLiteral("statements")] = sts;
    }
    if (!s.sourceSpeakers.isEmpty()) {
        QJsonObject ss;
        for (auto it = s.sourceSpeakers.cbegin(); it != s.sourceSpeakers.cend(); ++it) {
            QJsonObject so;
            so[QStringLiteral("key")]  = it->key;
            so[QStringLiteral("name")] = it->name;
            ss[it.key()] = so;
        }
        o[QStringLiteral("sourceSpeakers")] = ss;
    }
    return o;
}

Summary summaryFromJson(const QJsonObject& o)
{
    Summary s;
    s.execSummary = o.value(QStringLiteral("execSummary")).toString();
    s.decisions   = arrayToStringList(o.value(QStringLiteral("decisions")).toArray());

    const QJsonArray items = o.value(QStringLiteral("actionItems")).toArray();
    for (const auto& v : items)
        s.actionItems.append(actionItemFromJson(v.toObject()));

    s.participants = arrayToStringList(o.value(QStringLiteral("participants")).toArray());
    // Régi summary.json-ban ezek a mezők hiányoznak → üresek maradnak.
    s.openQuestions = arrayToStringList(o.value(QStringLiteral("openQuestions")).toArray());
    for (const auto& v : o.value(QStringLiteral("memo")).toArray()) {
        const QJsonObject mo = v.toObject();
        MemoSection m;
        m.title   = mo.value(QStringLiteral("title")).toString();
        m.startMs = static_cast<qint64>(mo.value(QStringLiteral("startMs")).toDouble(-1));
        m.endMs   = static_cast<qint64>(mo.value(QStringLiteral("endMs")).toDouble(-1));
        m.points  = arrayToStringList(mo.value(QStringLiteral("points")).toArray());
        m.speakers = arrayToStringList(mo.value(QStringLiteral("speakers")).toArray());
        if (!m.title.isEmpty() || !m.points.isEmpty())
            s.memo.append(m);
    }
    // Régi summary.json: nincs statements / sourceSpeakers → üres.
    for (const auto& v : o.value(QStringLiteral("statements")).toArray()) {
        const QJsonObject so = v.toObject();
        SummaryStatement st;
        st.id      = so.value(QStringLiteral("id")).toString();
        st.kind    = summarysrc::kindFromString(so.value(QStringLiteral("kind")).toString());
        st.text    = so.value(QStringLiteral("text")).toString();
        st.owner   = so.value(QStringLiteral("owner")).toString();
        st.flagged = so.value(QStringLiteral("flagged")).toBool();
        for (const auto& sv : so.value(QStringLiteral("sourceSpans")).toArray()) {
            const QJsonObject spo = sv.toObject();
            SourceSpan sp;
            sp.startMs = static_cast<qint64>(spo.value(QStringLiteral("startMs")).toDouble(-1));
            sp.endMs   = static_cast<qint64>(spo.value(QStringLiteral("endMs")).toDouble(-1));
            sp.utteranceIds = arrayToStringList(spo.value(QStringLiteral("utteranceIds")).toArray());
            st.sourceSpans.append(sp);
        }
        if (!st.id.isEmpty()) s.statements.append(st);
    }
    const QJsonObject ss = o.value(QStringLiteral("sourceSpeakers")).toObject();
    for (auto it = ss.constBegin(); it != ss.constEnd(); ++it) {
        const QJsonObject so = it.value().toObject();
        s.sourceSpeakers.insert(it.key(), SourceSpeaker{so.value(QStringLiteral("key")).toString(),
                                                        so.value(QStringLiteral("name")).toString()});
    }
    return s;
}

// ---- résztvevők / bizonyíték ---------------------------------------------
QString participantSourceName(ParticipantSource s)
{
    switch (s) {
    case ParticipantSource::Manual:   return QStringLiteral("manual");
    case ParticipantSource::Voice:    return QStringLiteral("voice");
    case ParticipantSource::Tag:      return QStringLiteral("tag");
    case ParticipantSource::Calendar: return QStringLiteral("calendar");
    }
    return QStringLiteral("voice");
}

ParticipantSource participantSourceFromName(const QString& s)
{
    if (s == QLatin1String("manual"))   return ParticipantSource::Manual;
    if (s == QLatin1String("tag"))      return ParticipantSource::Tag;
    if (s == QLatin1String("calendar")) return ParticipantSource::Calendar;
    return ParticipantSource::Voice;
}

QString participantGroupName(ParticipantGroup g)
{
    switch (g) {
    case ParticipantGroup::Sure:            return QStringLiteral("sure");
    case ParticipantGroup::Doubt:           return QStringLiteral("doubt");
    case ParticipantGroup::InvitedNotHeard: return QStringLiteral("invited");
    }
    return QStringLiteral("doubt");
}

ParticipantGroup participantGroupFromName(const QString& s)
{
    if (s == QLatin1String("sure"))    return ParticipantGroup::Sure;
    if (s == QLatin1String("invited")) return ParticipantGroup::InvitedNotHeard;
    return ParticipantGroup::Doubt;
}

QString evidenceKindName(EvidenceKind k)
{
    switch (k) {
    case EvidenceKind::Voice:      return QStringLiteral("voice");
    case EvidenceKind::Side:       return QStringLiteral("side");
    case EvidenceKind::Tag:        return QStringLiteral("tag");
    case EvidenceKind::LineCount:  return QStringLiteral("lineCount");
    case EvidenceKind::Similarity: return QStringLiteral("similarity");
    case EvidenceKind::Calendar:   return QStringLiteral("calendar");
    case EvidenceKind::Manual:     return QStringLiteral("manual");
    }
    return QStringLiteral("voice");
}

QString polarityName(Polarity p)
{
    switch (p) {
    case Polarity::Support:    return QStringLiteral("support");
    case Polarity::Contradict: return QStringLiteral("contradict");
    case Polarity::Neutral:    return QStringLiteral("neutral");
    }
    return QStringLiteral("neutral");
}

namespace {

EvidenceKind evidenceKindFromName(const QString& s)
{
    for (EvidenceKind k : {EvidenceKind::Voice, EvidenceKind::Side, EvidenceKind::Tag,
                           EvidenceKind::LineCount, EvidenceKind::Similarity,
                           EvidenceKind::Calendar, EvidenceKind::Manual})
        if (evidenceKindName(k) == s) return k;
    return EvidenceKind::Voice;
}

Polarity polarityFromName(const QString& s)
{
    if (s == QLatin1String("support"))    return Polarity::Support;
    if (s == QLatin1String("contradict")) return Polarity::Contradict;
    return Polarity::Neutral;
}

} // namespace

QJsonObject toJson(const Evidence& e)
{
    QJsonObject o;
    o[QStringLiteral("kind")]     = evidenceKindName(e.kind);
    o[QStringLiteral("polarity")] = polarityName(e.polarity);
    o[QStringLiteral("value")]    = e.value;
    o[QStringLiteral("text")]     = e.text;
    if (!e.detail.isEmpty())    o[QStringLiteral("detail")] = e.detail;
    if (!e.fixTarget.isEmpty()) o[QStringLiteral("fixTarget")] = e.fixTarget;
    return o;
}

Evidence evidenceFromJson(const QJsonObject& o)
{
    Evidence e;
    e.kind      = evidenceKindFromName(o.value(QStringLiteral("kind")).toString());
    e.polarity  = polarityFromName(o.value(QStringLiteral("polarity")).toString());
    e.value     = o.value(QStringLiteral("value")).toDouble();
    e.text      = o.value(QStringLiteral("text")).toString();
    e.detail    = o.value(QStringLiteral("detail")).toString();
    e.fixTarget = o.value(QStringLiteral("fixTarget")).toString();
    return e;
}

QJsonObject toJson(const Participant& p)
{
    QJsonObject o;
    o[QStringLiteral("id")]            = p.id;
    o[QStringLiteral("personName")]    = p.personName;
    o[QStringLiteral("rawSpeakerIds")] = stringListToArray(p.rawSpeakerIds);
    o[QStringLiteral("source")]        = participantSourceName(p.source);
    o[QStringLiteral("approved")]      = p.approved;
    o[QStringLiteral("sides")]         = stringListToArray(p.sides);
    QJsonArray ev;
    for (const Evidence& e : p.evidence) ev.append(toJson(e));
    o[QStringLiteral("evidence")]      = ev;
    o[QStringLiteral("talkShare")]     = p.talkShare;
    return o;
}

Participant participantFromJson(const QJsonObject& o)
{
    Participant p;
    p.id            = o.value(QStringLiteral("id")).toString();
    p.personName    = o.value(QStringLiteral("personName")).toString();
    p.rawSpeakerIds = arrayToStringList(o.value(QStringLiteral("rawSpeakerIds")).toArray());
    p.source        = participantSourceFromName(o.value(QStringLiteral("source")).toString());
    p.approved      = o.value(QStringLiteral("approved")).toBool();
    p.sides         = arrayToStringList(o.value(QStringLiteral("sides")).toArray());
    for (const QJsonValue& v : o.value(QStringLiteral("evidence")).toArray())
        p.evidence.append(evidenceFromJson(v.toObject()));
    p.talkShare     = o.value(QStringLiteral("talkShare")).toDouble();
    return p;
}

QJsonObject toJson(const ParticipantApproval& a)
{
    QJsonObject o;
    o[QStringLiteral("at")]       = a.at;
    o[QStringLiteral("modelIds")] = stringListToArray(a.modelIds);
    QJsonArray cs;
    for (const ParticipantApprovalEntry& c : a.candidates) {
        QJsonObject co;
        co[QStringLiteral("participantId")] = c.participantId;
        co[QStringLiteral("personName")]    = c.personName;
        co[QStringLiteral("group")]         = participantGroupName(c.group);
        co[QStringLiteral("checked")]       = c.checked;
        co[QStringLiteral("rawSpeakerIds")] = stringListToArray(c.rawSpeakerIds);
        cs.append(co);
    }
    o[QStringLiteral("candidates")] = cs;
    if (a.skipped) o[QStringLiteral("skipped")] = true;
    if (a.solo)    o[QStringLiteral("solo")] = true;
    return o;
}

ParticipantApproval participantApprovalFromJson(const QJsonObject& o)
{
    ParticipantApproval a;
    a.at       = o.value(QStringLiteral("at")).toString();
    a.modelIds = arrayToStringList(o.value(QStringLiteral("modelIds")).toArray());
    for (const QJsonValue& v : o.value(QStringLiteral("candidates")).toArray()) {
        const QJsonObject co = v.toObject();
        ParticipantApprovalEntry c;
        c.participantId = co.value(QStringLiteral("participantId")).toString();
        c.personName    = co.value(QStringLiteral("personName")).toString();
        c.group         = participantGroupFromName(co.value(QStringLiteral("group")).toString());
        c.checked       = co.value(QStringLiteral("checked")).toBool();
        c.rawSpeakerIds = arrayToStringList(co.value(QStringLiteral("rawSpeakerIds")).toArray());
        a.candidates.append(c);
    }
    a.skipped = o.value(QStringLiteral("skipped")).toBool();
    a.solo    = o.value(QStringLiteral("solo")).toBool();
    return a;
}

// ---- Meeting --------------------------------------------------------------
QJsonObject toJson(const Meeting& m)
{
    QJsonObject o;
    o[QStringLiteral("id")]        = m.id;
    o[QStringLiteral("title")]     = m.title;
    o[QStringLiteral("folder")]    = m.folder;
    // ISO-8601, hogy stabil és időzóna-tudatos legyen.
    o[QStringLiteral("startedAt")] = m.startedAt.toString(Qt::ISODateWithMs);
    o[QStringLiteral("durationMs")] = static_cast<double>(m.durationMs);

    QJsonArray tracks;
    for (const auto& t : m.tracks)
        tracks.append(toJson(t));
    o[QStringLiteral("tracks")] = tracks;

    o[QStringLiteral("mixdownFile")]   = m.mixdownFile;
    o[QStringLiteral("mixdownDirty")]  = m.mixdownDirty;
    o[QStringLiteral("hasTranscript")] = m.hasTranscript;
    o[QStringLiteral("hasSummary")]    = m.hasSummary;

    QJsonObject sm;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        sm[it.key()] = it.value();
    o[QStringLiteral("speakerMap")] = sm;
    if (!m.contextNote.isEmpty())
        o[QStringLiteral("contextNote")] = m.contextNote;
    if (!m.detectedCallApp.isEmpty())
        o[QStringLiteral("detectedCallApp")] = m.detectedCallApp;
    if (!m.tagIds.isEmpty())
        o[QStringLiteral("tags")] = QJsonArray::fromStringList(m.tagIds);
    if (!m.participants.isEmpty()) {
        QJsonArray ps;
        for (const Participant& p : m.participants) ps.append(toJson(p));
        o[QStringLiteral("participants")] = ps;
    }
    if (m.approval)
        o[QStringLiteral("participantApproval")] = toJson(*m.approval);
    return o;
}

Meeting meetingFromJson(const QJsonObject& o)
{
    Meeting m;
    m.id        = o.value(QStringLiteral("id")).toString();
    m.title     = o.value(QStringLiteral("title")).toString();
    m.folder    = o.value(QStringLiteral("folder")).toString();
    m.startedAt = QDateTime::fromString(o.value(QStringLiteral("startedAt")).toString(),
                                        Qt::ISODateWithMs);
    m.durationMs = static_cast<qint64>(o.value(QStringLiteral("durationMs")).toDouble());

    const QJsonArray tracks = o.value(QStringLiteral("tracks")).toArray();
    for (const auto& v : tracks)
        m.tracks.append(trackFromJson(v.toObject()));

    m.mixdownFile   = o.value(QStringLiteral("mixdownFile")).toString();
    m.mixdownDirty  = o.value(QStringLiteral("mixdownDirty")).toBool(false);
    m.hasTranscript = o.value(QStringLiteral("hasTranscript")).toBool();
    m.hasSummary    = o.value(QStringLiteral("hasSummary")).toBool();

    const QJsonObject sm = o.value(QStringLiteral("speakerMap")).toObject();
    for (auto it = sm.constBegin(); it != sm.constEnd(); ++it)
        m.speakerMap.insert(it.key(), it.value().toString());
    m.contextNote = o.value(QStringLiteral("contextNote")).toString();
    m.detectedCallApp = o.value(QStringLiteral("detectedCallApp")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("tags")).toArray()) {
        const QString id = v.toString();
        if (!id.isEmpty() && !m.tagIds.contains(id)) m.tagIds << id;
    }
    for (const QJsonValue& v : o.value(QStringLiteral("participants")).toArray())
        m.participants.append(participantFromJson(v.toObject()));
    if (o.value(QStringLiteral("participantApproval")).isObject())
        m.approval = participantApprovalFromJson(o.value(QStringLiteral("participantApproval")).toObject());
    // Régi meeting: a figyelő automatikus mondata („Automatikusan észlelt hívás: …”) nem
    // megjegyzés → üres megjegyzés + észlelt hívás. A következő mentés már így írja ki.
    meetingnotes::interpretLegacyNote(m);
    return m;
}

// ---- ProviderConfig -------------------------------------------------------
// FONTOS: az apiKey SZÁNDÉKOSAN NEM kerül a JSON-be (KeyStore felel érte).
QJsonObject toJson(const ProviderConfig& p)
{
    QJsonObject o;
    o[QStringLiteral("type")]    = p.type;
    o[QStringLiteral("baseUrl")] = p.baseUrl;
    o[QStringLiteral("model")]   = p.model;
    o[QStringLiteral("temperature")] = p.temperature;
    o[QStringLiteral("maxTokens")]   = p.maxTokens;
    o[QStringLiteral("reasoning")]   = p.reasoning;
    o[QStringLiteral("contextLength")] = p.contextLength;
    if (!p.extra.isEmpty())
        o[QStringLiteral("extra")] = QJsonObject::fromVariantMap(p.extra);
    return o;
}

ProviderConfig providerConfigFromJson(const QJsonObject& o)
{
    ProviderConfig p;
    p.type    = o.value(QStringLiteral("type")).toString();
    p.baseUrl = o.value(QStringLiteral("baseUrl")).toString();
    p.model   = o.value(QStringLiteral("model")).toString();
    // Visszafelé kompatibilis: hiányzó mezőnél a ProviderConfig-default marad.
    p.temperature = o.value(QStringLiteral("temperature")).toDouble(0.2);
    p.maxTokens   = o.value(QStringLiteral("maxTokens")).toInt(8000);
    p.reasoning   = o.value(QStringLiteral("reasoning")).toString(p.reasoning);
    p.contextLength = qMax(0, o.value(QStringLiteral("contextLength")).toInt(0));
    // apiKey-t SOHA nem olvasunk JSON-ből; futásidőben a KeyStore tölti.
    if (o.contains(QStringLiteral("extra")))
        p.extra = o.value(QStringLiteral("extra")).toObject().toVariantMap();
    return p;
}

// ---- AppSettings ----------------------------------------------------------
namespace {

// id -> ProviderConfig map → JSON-objektum (providerenként a meglévő
// toJson(ProviderConfig); az apiKey továbbra sem kerül bele).
QJsonObject providerConfigsToJson(const QMap<QString, ProviderConfig>& configs)
{
    QJsonObject o;
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it)
        o[it.key()] = toJson(it.value());
    return o;
}

QMap<QString, ProviderConfig> providerConfigsFromJson(const QJsonObject& o)
{
    QMap<QString, ProviderConfig> configs;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        configs.insert(it.key(), providerConfigFromJson(it.value().toObject()));
    return configs;
}

} // namespace

QJsonObject toJson(const AppSettings& s)
{
    QJsonObject o;
    o[QStringLiteral("audioDir")]       = s.audioDir;
    o[QStringLiteral("notesDir")]       = s.notesDir;
    o[QStringLiteral("metadataDir")]    = s.metadataDir;
    o[QStringLiteral("userSpeakerName")] = s.userSpeakerName;
    o[QStringLiteral("autoRecordAllDevices")] = s.autoRecordAllDevices;
    o[QStringLiteral("languageHints")]  = stringListToArray(s.languageHints);
    o[QStringLiteral("uiLanguage")]     = s.uiLanguage;
    o[QStringLiteral("audioQuality")]   = s.audioQuality;
    o[QStringLiteral("mixdownMode")]    = s.mixdownMode;
    o[QStringLiteral("summaryPrompt")]  = s.summaryPrompt;
    o[QStringLiteral("notesPrompt")]    = s.notesPrompt;
    o[QStringLiteral("mergePrompt")]    = s.mergePrompt;
    o[QStringLiteral("topicExtractionPrompt")] = s.topicExtractionPrompt;
    o[QStringLiteral("topicAnalysisPrompt")]   = s.topicAnalysisPrompt;
    o[QStringLiteral("summaryLanguage")]       = s.summaryLanguage;

    // Új multi-provider shape: kiválasztott id + providerenkénti config.
    o[QStringLiteral("sttProviderId")] = s.sttProviderId;
    o[QStringLiteral("llmProviderId")] = s.llmProviderId;
    o[QStringLiteral("sttProviders")]  = providerConfigsToJson(s.sttConfigs);
    o[QStringLiteral("llmProviders")]  = providerConfigsToJson(s.llmConfigs);
    o[QStringLiteral("embeddingProviderId")] = s.embeddingProviderId;
    o[QStringLiteral("embeddingProviders")]  = providerConfigsToJson(s.embeddingConfigs);
    o[QStringLiteral("tagSuggestions")]      = s.tagSuggestions;
    o[QStringLiteral("llmTagSuggestions")]   = s.llmTagSuggestions;
    o[QStringLiteral("voiceModels")]         = stringListToArray(VoiceModelRegistry::normalizeIds(s.voiceModels));

    // Meeting-figyelő (háttér-detektor + tray).
    o[QStringLiteral("detectorEnabled")]     = s.detectorEnabled;
    o[QStringLiteral("detectorIntervalSec")] = s.detectorIntervalSec;
    o[QStringLiteral("watcherAutostart")]    = s.watcherAutostart;
    o[QStringLiteral("askStopOnCallEnd")]    = s.askStopOnCallEnd;
    o[QStringLiteral("silenceAskMinutes")]   = s.silenceAskMinutes;
    o[QStringLiteral("detectorId")]          = s.detectorId;
    o[QStringLiteral("knownCallApps")]       = stringListToArray(s.knownCallApps);

    // Tanara Cloud.
    o[QStringLiteral("cloudEnabled")]  = s.cloudEnabled;
    o[QStringLiteral("cloudBaseUrl")]  = s.cloudBaseUrl;
    o[QStringLiteral("cloudSttTier")]  = s.cloudSttTier;
    o[QStringLiteral("cloudLlmTier")]  = s.cloudLlmTier;
    o[QStringLiteral("cloudSttModel")] = s.cloudSttModel;
    o[QStringLiteral("cloudLlmModel")] = s.cloudLlmModel;
    o[QStringLiteral("waitlistEmail")] = s.waitlistEmail;
    o[QStringLiteral("cloudEstimateBeforeRun")] = s.cloudEstimateBeforeRun;
    o[QStringLiteral("onboardingDone")] = s.onboardingDone;

    // Hangeszközök felhasználói nevei (nyers név → barátságos név).
    QJsonObject names;
    for (auto it = s.deviceNames.constBegin(); it != s.deviceNames.constEnd(); ++it)
        names[it.key()] = it.value();
    o[QStringLiteral("deviceNames")] = names;
    return o;
}

AppSettings appSettingsFromJson(const QJsonObject& o)
{
    AppSettings s;
    s.audioDir        = o.value(QStringLiteral("audioDir")).toString();
    s.notesDir        = o.value(QStringLiteral("notesDir")).toString();
    s.metadataDir     = o.value(QStringLiteral("metadataDir")).toString();
    s.userSpeakerName = o.value(QStringLiteral("userSpeakerName")).toString();
    s.autoRecordAllDevices = o.value(QStringLiteral("autoRecordAllDevices")).toBool(true);
    if (o.contains(QStringLiteral("languageHints")))
        s.languageHints = arrayToStringList(o.value(QStringLiteral("languageHints")).toArray());
    s.audioQuality = o.value(QStringLiteral("audioQuality")).toString(s.audioQuality);
    s.mixdownMode  = o.value(QStringLiteral("mixdownMode")).toString(s.mixdownMode);
    s.summaryPrompt = o.value(QStringLiteral("summaryPrompt")).toString(s.summaryPrompt);
    s.notesPrompt   = o.value(QStringLiteral("notesPrompt")).toString(s.notesPrompt);
    s.mergePrompt   = o.value(QStringLiteral("mergePrompt")).toString(s.mergePrompt);
    s.topicExtractionPrompt = o.value(QStringLiteral("topicExtractionPrompt")).toString(s.topicExtractionPrompt);
    s.topicAnalysisPrompt   = o.value(QStringLiteral("topicAnalysisPrompt")).toString(s.topicAnalysisPrompt);

    // STT: új shape (sttProviders) elsőbbség; különben migráció a régi `stt`-ből.
    if (o.contains(QStringLiteral("sttProviders"))) {
        s.sttProviderId = o.value(QStringLiteral("sttProviderId"))
                              .toString(s.sttProviderId);
        s.sttConfigs = providerConfigsFromJson(
            o.value(QStringLiteral("sttProviders")).toObject());
    } else if (o.contains(QStringLiteral("stt"))) {
        const ProviderConfig cfg =
            providerConfigFromJson(o.value(QStringLiteral("stt")).toObject());
        const QString id = cfg.type.isEmpty()
                               ? QStringLiteral("soniox")
                               : cfg.type;
        s.sttProviderId = id;
        s.sttConfigs.insert(id, cfg);
    }

    // LLM: ugyanígy, openai-compat fallback id-vel.
    if (o.contains(QStringLiteral("llmProviders"))) {
        s.llmProviderId = o.value(QStringLiteral("llmProviderId"))
                              .toString(s.llmProviderId);
        s.llmConfigs = providerConfigsFromJson(
            o.value(QStringLiteral("llmProviders")).toObject());
    } else if (o.contains(QStringLiteral("llm"))) {
        const ProviderConfig cfg =
            providerConfigFromJson(o.value(QStringLiteral("llm")).toObject());
        const QString id = cfg.type.isEmpty()
                               ? QStringLiteral("openai-compat")
                               : cfg.type;
        s.llmProviderId = id;
        s.llmConfigs.insert(id, cfg);
    }

    s.embeddingProviderId = o.value(QStringLiteral("embeddingProviderId")).toString();
    s.embeddingConfigs = providerConfigsFromJson(o.value(QStringLiteral("embeddingProviders")).toObject());
    s.tagSuggestions    = o.value(QStringLiteral("tagSuggestions")).toBool(s.tagSuggestions);
    s.llmTagSuggestions = o.value(QStringLiteral("llmTagSuggestions")).toBool(s.llmTagSuggestions);
    // Beszélő-modellek: hiányzó kulcs → alapérték; az üres lista érvényes (minden modell ki).
    if (o.contains(QStringLiteral("voiceModels")))
        s.voiceModels = VoiceModelRegistry::normalizeIds(
            arrayToStringList(o.value(QStringLiteral("voiceModels")).toArray()));

    s.uiLanguage = o.value(QStringLiteral("uiLanguage")).toString(s.uiLanguage);
    s.summaryLanguage = o.value(QStringLiteral("summaryLanguage")).toString(s.summaryLanguage);

    // Meeting-figyelő — safe-merge a defaultokkal (a régi settings.json e nélkül tölt).
    s.detectorEnabled     = o.value(QStringLiteral("detectorEnabled")).toBool(s.detectorEnabled);
    s.detectorIntervalSec = o.value(QStringLiteral("detectorIntervalSec")).toInt(s.detectorIntervalSec);
    s.watcherAutostart    = o.value(QStringLiteral("watcherAutostart")).toBool(s.watcherAutostart);
    s.askStopOnCallEnd    = o.value(QStringLiteral("askStopOnCallEnd")).toBool(s.askStopOnCallEnd);
    s.silenceAskMinutes   = o.value(QStringLiteral("silenceAskMinutes")).toInt(s.silenceAskMinutes);
    s.detectorId          = o.value(QStringLiteral("detectorId")).toString(s.detectorId);
    if (o.contains(QStringLiteral("knownCallApps")))
        s.knownCallApps = arrayToStringList(o.value(QStringLiteral("knownCallApps")).toArray());

    // Tanara Cloud — safe-merge a defaultokkal.
    s.cloudEnabled  = o.value(QStringLiteral("cloudEnabled")).toBool(s.cloudEnabled);
    s.cloudBaseUrl  = o.value(QStringLiteral("cloudBaseUrl")).toString(s.cloudBaseUrl);
    s.cloudSttTier  = o.value(QStringLiteral("cloudSttTier")).toString(s.cloudSttTier);
    s.cloudLlmTier  = o.value(QStringLiteral("cloudLlmTier")).toString(s.cloudLlmTier);
    s.cloudSttModel = o.value(QStringLiteral("cloudSttModel")).toString(s.cloudSttModel);
    s.cloudLlmModel = o.value(QStringLiteral("cloudLlmModel")).toString(s.cloudLlmModel);
    s.waitlistEmail = o.value(QStringLiteral("waitlistEmail")).toString(s.waitlistEmail);
    s.cloudEstimateBeforeRun =
        o.value(QStringLiteral("cloudEstimateBeforeRun")).toBool(s.cloudEstimateBeforeRun);
    s.onboardingDone = o.value(QStringLiteral("onboardingDone")).toBool(s.onboardingDone);

    // Hangeszközök felhasználói nevei — az üres név nem felülírás (kimarad).
    const QJsonObject names = o.value(QStringLiteral("deviceNames")).toObject();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        const QString name = it.value().toString().simplified();
        if (!it.key().isEmpty() && !name.isEmpty())
            s.deviceNames.insert(it.key(), name);
    }
    return s;
}

} // namespace tanara
