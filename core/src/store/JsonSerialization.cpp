#include "tanara/store/JsonSerialization.h"

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
    return s;
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
    // apiKey-t SOHA nem olvasunk JSON-ből; futásidőben a KeyStore tölti.
    if (o.contains(QStringLiteral("extra")))
        p.extra = o.value(QStringLiteral("extra")).toObject().toVariantMap();
    return p;
}

// ---- AppSettings ----------------------------------------------------------
QJsonObject toJson(const AppSettings& s)
{
    QJsonObject o;
    o[QStringLiteral("audioDir")]       = s.audioDir;
    o[QStringLiteral("notesDir")]       = s.notesDir;
    o[QStringLiteral("metadataDir")]    = s.metadataDir;
    o[QStringLiteral("userSpeakerName")] = s.userSpeakerName;
    o[QStringLiteral("autoRecordAllDevices")] = s.autoRecordAllDevices;
    o[QStringLiteral("languageHints")]  = stringListToArray(s.languageHints);
    o[QStringLiteral("stt")]            = toJson(s.stt);
    o[QStringLiteral("llm")]            = toJson(s.llm);
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
    s.stt = providerConfigFromJson(o.value(QStringLiteral("stt")).toObject());
    s.llm = providerConfigFromJson(o.value(QStringLiteral("llm")).toObject());
    return s;
}

} // namespace tanara
