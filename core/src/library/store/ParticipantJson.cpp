#include "tanara/library/store/ParticipantJson.h"
#include "tanara/domain/SpeakerKeys.h"

#include "tanara/domain/EvidenceKeys.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace tanara::library {

namespace {
QJsonArray stringListToArray(const QStringList& list) { return QJsonArray::fromStringList(list); }
QStringList arrayToStringList(const QJsonArray& arr)
{
    QStringList list;
    for (const auto& v : arr) list.append(v.toString());
    return list;
}
} // namespace

// ---- participants / evidence ---------------------------------------------
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

// An evidence read from an old file that has only the display text (written in the language active when
// the analysis ran): map the known Hungarian / English texts to the key + params they were made from. Anything
// else keeps its text verbatim under the "legacy" key.
void mapLegacyText(Evidence& e, const QString& text)
{
    namespace K = evidencekeys;
    auto setKey = [&e](const QLatin1String& key) { e.key = key; };
    static const QRegularExpression score(QStringLiteral("^(hang|voice) \\d+%$"));
    auto sideOfLabel = [](const QString& l) {
        if (l == QStringLiteral("mikrofon") || l == QStringLiteral("microphone")) return QStringLiteral("mic");
        if (l == QStringLiteral("hívás") || l == QStringLiteral("call")) return QStringLiteral("loopback");
        return QString();
    };
    auto sidesOf = [&](const QString& labels) {
        QStringList sides;
        for (const QString& part : labels.split(QRegularExpression(QStringLiteral(" (és|and) ")))) {
            const QString s = sideOfLabel(part.trimmed());
            if (s.isEmpty()) return QStringList();
            sides << s;
        }
        return sides;
    };
    switch (e.kind) {
    case EvidenceKind::Manual:
        if (text == QStringLiteral("kézzel felvéve") || text == QStringLiteral("added by hand")) { setKey(K::kManualAdded); return; }
        break;
    case EvidenceKind::Voice:
        if (score.match(text).hasMatch()) { setKey(K::kVoiceScore); return; }
        if (text == QStringLiteral("ismeretlen hang") || text == QStringLiteral("unknown voice")) { setKey(K::kVoiceUnknown); return; }
        if (text == QStringLiteral("nem hallottuk")) { setKey(K::kVoiceNotHeard); return; }
        if (text == QStringLiteral("nincs lenyomata")) { setKey(K::kVoiceNoVoiceprint); return; }
        break;
    case EvidenceKind::Side: {
        if (text == QStringLiteral("csak a hívás hangján")) { setKey(K::kSideCallOnly); return; }
        QString labels = text;
        bool manual = false;
        for (const QString& prefix : {QStringLiteral("kézzel: "), QStringLiteral("by hand: ")})
            if (text.startsWith(prefix)) { labels = text.mid(prefix.size()); manual = true; }
        const QStringList sides = sidesOf(labels);
        if (!sides.isEmpty()) {
            setKey(manual ? K::kManualTracks : K::kSideHeard);
            e.params.insert(K::kParamSides, sides);
            return;
        }
        break;
    }
    case EvidenceKind::Tag:
        if (text == QStringLiteral("más címkék") || text == QStringLiteral("other tags")) { setKey(K::kTagOther); return; }
        if (text.startsWith(QLatin1Char('#'))) { setKey(K::kTagMatch); e.params.insert(K::kParamChip, text); return; }
        break;
    default:
        break;
    }
    setKey(K::kLegacy);
    e.params.insert(K::kParamText, text);
}

} // namespace

QJsonObject toJson(const Evidence& e)
{
    QJsonObject o;
    o[QStringLiteral("kind")]     = evidenceKindName(e.kind);
    o[QStringLiteral("polarity")] = polarityName(e.polarity);
    o[QStringLiteral("value")]    = e.value;
    // A keyed evidence is language-neutral: key + params, no text (it is rendered at display time).
    if (!e.key.isEmpty()) {
        o[QStringLiteral("key")] = e.key;
        if (!e.params.isEmpty()) o[QStringLiteral("params")] = QJsonObject::fromVariantMap(e.params);
    } else {
        o[QStringLiteral("text")] = e.text;
    }
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
    e.detail    = o.value(QStringLiteral("detail")).toString();
    e.fixTarget = o.value(QStringLiteral("fixTarget")).toString();
    if (o.contains(QStringLiteral("key"))) {
        e.key    = o.value(QStringLiteral("key")).toString();
        e.params = o.value(QStringLiteral("params")).toObject().toVariantMap();
    } else {
        mapLegacyText(e, o.value(QStringLiteral("text")).toString());
    }
    return e;
}

// ---- the participant's sample and "same voice" merges (UA1; additive, absent = empty) ----
static void writeSampleAndMerges(QJsonObject& o, const Participant& p)
{
    if (!p.sampleTrackId.isEmpty()) o[QStringLiteral("sampleTrackId")] = p.sampleTrackId;
    if (!p.sampleRef.isEmpty()) o[QStringLiteral("sampleRef")] = p.sampleRef;
    if (!p.mergedFrom.isEmpty()) o[QStringLiteral("mergedFrom")] = stringListToArray(p.mergedFrom);
}

static void readSampleAndMerges(const QJsonObject& o, Participant& p)
{
    p.sampleTrackId = o.value(QStringLiteral("sampleTrackId")).toString();
    p.sampleRef = o.value(QStringLiteral("sampleRef")).toString();
    p.mergedFrom = arrayToStringList(o.value(QStringLiteral("mergedFrom")).toArray());
}

QJsonObject toJson(const Participant& p)
{
    QJsonObject o;
    o[QStringLiteral("id")]            = p.id;
    if (!p.personId.isNull()) o[QStringLiteral("personId")] = p.personId.toString();
    o[QStringLiteral("rawSpeakerIds")] = stringListToArray(p.rawSpeakerIds);
    o[QStringLiteral("source")]        = participantSourceName(p.source);
    o[QStringLiteral("approved")]      = p.approved;
    if (p.namedByHand) o[QStringLiteral("namedByHand")] = true;
    o[QStringLiteral("sides")]         = stringListToArray(p.sides);
    if (!p.tracks.isEmpty()) o[QStringLiteral("tracks")] = stringListToArray(p.tracks);
    QJsonArray ev;
    for (const Evidence& e : p.evidence) ev.append(toJson(e));
    o[QStringLiteral("evidence")]      = ev;
    o[QStringLiteral("talkShare")]     = p.talkShare;
    writeSampleAndMerges(o, p);
    return o;
}

Participant participantFromJson(const QJsonObject& o)
{
    Participant p;
    p.id            = o.value(QStringLiteral("id")).toString();
    p.personId      = PersonId::fromString(o.value(QStringLiteral("personId")).toString());
    p.rawSpeakerIds = speakerkeys::normalizeSpeakerLabels(arrayToStringList(o.value(QStringLiteral("rawSpeakerIds")).toArray()));
    p.source        = participantSourceFromName(o.value(QStringLiteral("source")).toString());
    p.approved      = o.value(QStringLiteral("approved")).toBool();
    p.namedByHand   = o.value(QStringLiteral("namedByHand")).toBool();
    p.sides         = arrayToStringList(o.value(QStringLiteral("sides")).toArray());
    p.tracks        = arrayToStringList(o.value(QStringLiteral("tracks")).toArray());
    for (const QJsonValue& v : o.value(QStringLiteral("evidence")).toArray())
        p.evidence.append(evidenceFromJson(v.toObject()));
    p.talkShare     = o.value(QStringLiteral("talkShare")).toDouble();
    readSampleAndMerges(o, p);
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
        if (!c.personId.isNull()) co[QStringLiteral("personId")] = c.personId.toString();
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
        c.personId      = PersonId::fromString(co.value(QStringLiteral("personId")).toString());
        c.group         = participantGroupFromName(co.value(QStringLiteral("group")).toString());
        c.checked       = co.value(QStringLiteral("checked")).toBool();
        c.rawSpeakerIds = speakerkeys::normalizeSpeakerLabels(arrayToStringList(co.value(QStringLiteral("rawSpeakerIds")).toArray()));
        a.candidates.append(c);
    }
    a.skipped = o.value(QStringLiteral("skipped")).toBool();
    a.solo    = o.value(QStringLiteral("solo")).toBool();
    return a;
}

} // namespace tanara::library
