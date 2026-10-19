#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/domain/SpeakerKeys.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/foundation/log/Logging.h"

#include <QJsonArray>
#include <QJsonValue>

namespace tanara::library {

namespace {

// Enum <-> string, so the JSON stays human-readable and stable.
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
    if (!t.continuesTrackId.isEmpty())
        o[QStringLiteral("continuesTrackId")] = t.continuesTrackId;
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
    t.active       = o.value(QStringLiteral("active")).toBool(true);   // old recording -> active
    t.peakLevel    = static_cast<float>(o.value(QStringLiteral("peakLevel")).toDouble(0.0));
    t.customName   = o.value(QStringLiteral("customName")).toString();
    t.startOffsetMs = qint64(o.value(QStringLiteral("startOffsetMs")).toDouble(0.0));
    t.speechRatio  = o.value(QStringLiteral("speechRatio")).toDouble(-1.0);
    t.excludedReason = o.value(QStringLiteral("excludedReason")).toString();
    t.continuesTrackId = o.value(QStringLiteral("continuesTrackId")).toString();
    // Old meeting: a post-recording silence drop (active=false) without a reason -> "no speech".
    if (!t.active && t.excludedReason.isEmpty())
        t.excludedReason = QStringLiteral("noSpeech");
    return t;
}

// ---- RecordingGap ---------------------------------------------------------
QJsonObject toJson(const RecordingGap& g)
{
    QJsonObject o;
    o[QStringLiteral("fromMs")] = double(g.fromMs);
    o[QStringLiteral("toMs")]   = double(g.toMs);
    o[QStringLiteral("reason")] = g.reason;
    if (g.fromWall.isValid()) o[QStringLiteral("fromWall")] = g.fromWall.toString(Qt::ISODateWithMs);
    if (g.toWall.isValid())   o[QStringLiteral("toWall")]   = g.toWall.toString(Qt::ISODateWithMs);
    if (!g.trackId.isEmpty()) o[QStringLiteral("trackId")]  = g.trackId;
    return o;
}

RecordingGap recordingGapFromJson(const QJsonObject& o)
{
    RecordingGap g;
    g.fromMs   = qint64(o.value(QStringLiteral("fromMs")).toDouble());
    g.toMs     = qint64(o.value(QStringLiteral("toMs")).toDouble());
    g.reason   = o.value(QStringLiteral("reason")).toString();
    g.fromWall = QDateTime::fromString(o.value(QStringLiteral("fromWall")).toString(), Qt::ISODateWithMs);
    g.toWall   = QDateTime::fromString(o.value(QStringLiteral("toWall")).toString(), Qt::ISODateWithMs);
    g.trackId  = o.value(QStringLiteral("trackId")).toString();
    return g;
}

// ---- Meeting --------------------------------------------------------------
QJsonObject toJson(const Meeting& m)
{
    QJsonObject o;
    o[QStringLiteral("version")]   = kMeetingFormatVersion;
    o[QStringLiteral("id")]        = m.id;
    o[QStringLiteral("title")]     = m.title;
    // No "folder": the folder is where the file is (MeetingRepository sets it on reading), so a
    // meeting folder can move between recording roots without rewriting meeting.json.
    // ISO-8601, to be stable and time-zone-aware.
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
        sm[it.key()] = it.value().toString();
    o[QStringLiteral("speakerMap")] = sm;
    if (!m.contextNote.isEmpty())
        o[QStringLiteral("contextNote")] = m.contextNote;
    if (m.diarizeSpeakerCount > 0)
        o[QStringLiteral("diarizeSpeakers")] = m.diarizeSpeakerCount;
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
    if (!m.gaps.isEmpty()) {
        QJsonArray gs;
        for (const RecordingGap& g : m.gaps) gs.append(toJson(g));
        o[QStringLiteral("gaps")] = gs;
    }
    return o;
}

Meeting meetingFromJson(const QJsonObject& o)
{
    Meeting m;
    m.id        = o.value(QStringLiteral("id")).toString();
    m.title     = o.value(QStringLiteral("title")).toString();
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
    for (auto it = sm.constBegin(); it != sm.constEnd(); ++it) {
        const PersonId person = PersonId::fromString(it.value().toString());
        if (!person.isNull()) m.speakerMap.insert(speakerkeys::normalizeSpeakerLabel(it.key()), person);   // legacy "Beszélő N"
    }
    m.contextNote = o.value(QStringLiteral("contextNote")).toString();
    m.diarizeSpeakerCount = qMax(0, o.value(QStringLiteral("diarizeSpeakers")).toInt());
    m.detectedCallApp = o.value(QStringLiteral("detectedCallApp")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("tags")).toArray()) {
        const QString id = v.toString();
        if (!id.isEmpty() && !m.tagIds.contains(id)) m.tagIds << id;
    }
    for (const QJsonValue& v : o.value(QStringLiteral("participants")).toArray())
        m.participants.append(participantFromJson(v.toObject()));
    if (o.value(QStringLiteral("participantApproval")).isObject())
        m.approval = participantApprovalFromJson(o.value(QStringLiteral("participantApproval")).toObject());
    for (const QJsonValue& v : o.value(QStringLiteral("gaps")).toArray())
        m.gaps.append(recordingGapFromJson(v.toObject()));
    // An older meeting.json (before version 3) refers to people by NAME; the app does not read that.
    const int version = o.value(QStringLiteral("version")).toInt(0);
    if (version < kMeetingFormatVersion && (!sm.isEmpty() || !m.participants.isEmpty()))
        qCWarning(lcStore).noquote()
            << QStringLiteral("meeting.json %1 is in an old, name-keyed format (version %2): its people are not"
                              " resolved. Run: tanara-cli migrate person-ids").arg(m.id).arg(version);
    // Old meeting: the watcher's automatic sentence ("Automatically detected call: ...") is not a note
    // -> an empty note + the detected call. The next save writes it this way.
    meetingnotes::interpretLegacyNote(m);
    return m;
}

} // namespace tanara::library
