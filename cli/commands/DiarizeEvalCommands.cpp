// tanara-cli diarize-ref / diarize-eval (D6): the evaluation harness of the local diarization.
// All output is English, no tr(). Read-only: no meeting is changed.
//
//   diarize-ref <meeting-folder> [--out <file>] [--majority <share>]
//     builds a reference (DiarizationEval.h JSON) from the meeting's provider transcript
//     (transcript.tokens.json with speaker labels, mapped to people by the meeting's speakerMap):
//       * per-track tokens (the token trackId is a track): the track's tokens; a person heard on several
//         tracks belongs to the one with most speech, the copies elsewhere are leakage and are dropped;
//       * mixdown tokens: a person goes to the track bound to them by hand (a participant with exactly one
//         manual track), else to the track that is loudest - relative to its own speech level - under most
//         of their utterances (>= --majority, default 0.75); an ambiguous person's utterances are excluded
//         from scoring on every track.
//     The oracle is imperfect: provider diarization errors, emission-time tokens (padded points, not
//     audio ranges), non-lexical sounds absent, the mixdown heard once per voice.
//
//   diarize-eval <audio|meeting-folder> --ref <json|rttm> [...]: segmentation + one embedding pass, then
//     every (model set x threshold) clustering against the reference: DER (missed / false alarm /
//     confusion, collar) and the speaker-count error per track and in total; timing and peak RSS.
#include "DiarizeCommands.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/diarization/DiarizationEval.h"
#include "tanara/processing/diarization/LocalDiarizer.h"
#include "tanara/processing/diarization/PyannoteSegmenter.h"
#include "tanara/processing/diarization/TrackLeakage.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QTextStream>

#include <cstring>
#include <memory>


namespace tanara::cli {

namespace {

using namespace processing;

QTextStream& sout() { static QTextStream s(stdout); return s; }
QTextStream& serr() { static QTextStream s(stderr); return s; }

// Peak resident set size of this process in MB (VmHWM of /proc/self/status; -1 where there is none).
double peakRssMb()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray& line : f.readAll().split('\n'))
        if (line.startsWith("VmHWM:")) return line.mid(6).trimmed().split(' ').first().toDouble() / 1024.0;   // kB
    return -1;
}

QVector<float> decode16k(const media::MediaTool& media, const QString& path, QString* error)
{
    media::DecodeSpec ds;
    ds.path = path;
    ds.format = media::PcmFormat{16000, 1, media::SampleFormat::F32};
    QVector<float> pcm;
    const media::MediaResult dr = media.decodePcm(ds, [&pcm](const char* data, qsizetype bytes) {
        const qsizetype n = bytes / qsizetype(sizeof(float)), at = pcm.size();
        pcm.resize(at + n);
        std::memcpy(pcm.data() + at, data, size_t(n) * sizeof(float));
    });
    if (pcm.isEmpty() && error) *error = dr.errorText();
    return pcm;
}

// The few meeting.json fields the tools need (read-only, without the library's store).
struct MeetingInfo {
    struct TrackInfo {
        QString id, file, kind;
        qint64 startOffsetMs = 0;
        bool included = true;
    };
    QString folder;
    QVector<TrackInfo> tracks;
    QMap<QString, QString> speakerMap;          // raw label -> person id
    QMap<QString, QString> personTrack;         // person id -> its ONE manual track
};

bool loadMeeting(const QString& folder, MeetingInfo* m, QString* error)
{
    QFile f(QDir(folder).filePath(QStringLiteral("meeting.json")));
    if (!f.open(QIODevice::ReadOnly)) { *error = QStringLiteral("no meeting.json in %1").arg(folder); return false; }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m->folder = folder;
    for (const QJsonValue& v : o.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject t = v.toObject();
        MeetingInfo::TrackInfo ti;
        ti.id = t.value(QStringLiteral("id")).toString();
        ti.file = t.value(QStringLiteral("file")).toString();
        ti.kind = t.value(QStringLiteral("kind")).toString();
        ti.startOffsetMs = qint64(t.value(QStringLiteral("startOffsetMs")).toDouble());
        ti.included = t.value(QStringLiteral("active")).toBool(true)
                      && t.value(QStringLiteral("excludedReason")).toString().isEmpty() && !ti.file.isEmpty();
        m->tracks.append(ti);
    }
    const QJsonObject sm = o.value(QStringLiteral("speakerMap")).toObject();
    for (auto it = sm.begin(); it != sm.end(); ++it) m->speakerMap.insert(it.key(), it.value().toString());
    for (const QJsonValue& v : o.value(QStringLiteral("participants")).toArray()) {
        const QJsonObject p = v.toObject();
        const QString person = p.value(QStringLiteral("personId")).toString();
        const QJsonArray tr = p.value(QStringLiteral("tracks")).toArray();
        if (!person.isEmpty() && tr.size() == 1) m->personTrack.insert(person, tr.first().toString());
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------
// diarize-ref

const char* kRefUsage = "Usage: diarize-ref <meeting-folder> [--out <file>] [--majority <share>]\n";

} // namespace

int runDiarizeRefCommand(const QStringList& args)
{
    QString folder, outPath;
    double majority = 0.75;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args[i];
        bool ok = true;
        if (a == "--out" && i + 1 < args.size()) outPath = args[++i];
        else if (a == "--majority" && i + 1 < args.size()) majority = args[++i].toDouble(&ok);
        else if (a.startsWith("--") || !folder.isEmpty()) ok = false;
        else folder = a;
        if (!ok) { serr() << kRefUsage; return 2; }
    }
    if (folder.isEmpty()) { serr() << kRefUsage; return 2; }
    MeetingInfo m;
    QString error;
    if (!loadMeeting(folder, &m, &error)) { serr() << "ERROR: " << error << "\n"; return 1; }
    QFile tf(QDir(folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (!tf.open(QIODevice::ReadOnly)) { serr() << "ERROR: no transcript.tokens.json\n"; return 1; }
    const QJsonObject tokRoot = QJsonDocument::fromJson(tf.readAll()).object();

    // Tokens -> (person key, track) groups.
    struct Tok { diareval::RefToken t; QString track; };
    QVector<Tok> toks;
    for (const QJsonValue& v : tokRoot.value(QStringLiteral("tokens")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString label = o.value(QStringLiteral("speaker")).toString();
        if (label.isEmpty()) continue;
        Tok t;
        t.t.startMs = qint64(o.value(QStringLiteral("startMs")).toDouble());
        t.t.endMs = qint64(o.value(QStringLiteral("endMs")).toDouble());
        t.t.speaker = m.speakerMap.value(label, label);
        t.track = o.value(QStringLiteral("trackId")).toString();
        toks.append(t);
    }
    QStringList trackIds;
    for (const auto& t : m.tracks)
        if (t.included) trackIds << t.id;
    bool perTrack = !toks.isEmpty();
    for (const Tok& t : toks) perTrack = perTrack && trackIds.contains(t.track);

    diareval::Reference ref;
    ref.source = tokRoot.value(QStringLiteral("timing")).toString() == QLatin1String("emission")
                     ? QStringLiteral("soniox (emission-time tokens)") : QStringLiteral("provider tokens");
    ref.notes << QStringLiteral("Built by tanara-cli diarize-ref from %1 (speaker labels mapped by the speakerMap).")
                     .arg(QFileInfo(folder).fileName());
    // Track levels: per track its frame dB, its noise floor (the 10th percentile of all frames) and its
    // speech level (the 90th percentile inside the transcript's utterances).
    const auto media = media::FfmpegMediaTool::fromSettings(app::SettingsManager().settings().media);
    const leakage::Options lo;
    QMap<QString, QVector<float>> db;
    QMap<QString, float> level, floorDb;
    {
        QMap<QString, QVector<diareval::RefToken>> byPerson;
        for (const Tok& t : toks) byPerson[t.t.speaker].append(t.t);
        QVector<leakage::Turn> allUtt;
        for (auto it = byPerson.constBegin(); it != byPerson.constEnd(); ++it)
            for (const diareval::Interval& u : diareval::utterancesOf(it.value())) allUtt.append({u.start, u.end});
        for (const auto& t : m.tracks) {
            if (!t.included) continue;
            QString err;
            const QVector<float> pcm = decode16k(*media, QDir(folder).filePath(t.file), &err);
            if (pcm.isEmpty()) { serr() << "ERROR: cannot decode " << t.file << ": " << err << "\n"; return 1; }
            QVector<leakage::Turn> local;   // the utterances in track time
            for (const leakage::Turn& u : allUtt) local.append({u.start - t.startOffsetMs / 1000.0, u.end - t.startOffsetMs / 1000.0});
            const QVector<float> d = leakage::frameDb(pcm, lo.frameMs);
            db.insert(t.id, d);
            level.insert(t.id, leakage::speechLevel(d, local, lo.frameMs, lo.speechPercentile));
            floorDb.insert(t.id, leakage::speechLevel(d, {{0.0, d.size() * lo.frameMs / 1000.0}}, lo.frameMs, 0.10));
        }
    }
    // Per utterance the loudest track relative to its own speech level, among the tracks that really sound
    // there (>= 15 dB above their noise floor, above -70 dBFS; a silent / digital-zero track never wins).
    auto votesOf = [&](const QVector<diareval::RefToken>& tokens) {
        QHash<QString, double> votes;
        for (const diareval::Interval& u : diareval::utterancesOf(tokens)) {
            QString best;
            float bestRel = -1e9f;
            for (const auto& t : m.tracks) {
                if (!t.included) continue;
                const double off = t.startOffsetMs / 1000.0;
                const float abs = leakage::meanLevel(db.value(t.id), u.start - off, u.end - off, lo.frameMs);
                if (abs < -70.0f || abs - floorDb.value(t.id) < 15.0f) continue;
                const float rel = abs - level.value(t.id);
                if (rel > bestRel) { bestRel = rel; best = t.id; }
            }
            if (!best.isEmpty() && bestRel >= -10.0f) votes[best] += u.end - u.start;
        }
        return votes;
    };
    auto votesText = [](const QHash<QString, double>& votes) {
        QStringList v;
        for (auto x = votes.constBegin(); x != votes.constEnd(); ++x)
            v << QStringLiteral("%1 %2 s").arg(x.key()).arg(x.value(), 0, 'f', 1);
        v.sort();
        return v.isEmpty() ? QStringLiteral("none") : v.join(QStringLiteral(", "));
    };

    // Groups: (person, token track) in per-track mode, (person, "") for a mixdown transcript.
    QMap<QPair<QString, QString>, QVector<diareval::RefToken>> groups;
    for (const Tok& t : toks) groups[{t.t.speaker, perTrack ? t.track : QString()}].append(t.t);
    QMap<QPair<QString, QString>, QString> groupTrack;   // -> the track it is scored on ("" = excluded)
    if (perTrack)
        ref.notes << QStringLiteral("Per-track transcript: a speaker's tokens stay on their track unless another track "
                                    "is clearly louder under them (>= %1 of the time, relative to each track's speech "
                                    "level): then they are leakage (an echo) and dropped.").arg(majority);
    else
        ref.notes << QStringLiteral("Mixdown transcript: a person goes to the hand-bound track, else to the track "
                                    "loudest (relative to its own speech level, among the sounding tracks) under >= %1 "
                                    "of their utterance time; ambiguous persons are excluded from scoring.").arg(majority);
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QString person = it.key().first, own = it.key().second;
        if (!perTrack && m.personTrack.contains(person) && trackIds.contains(m.personTrack.value(person))) {
            groupTrack.insert(it.key(), m.personTrack.value(person));
            ref.notes << QStringLiteral("Person %1: bound by hand to %2").arg(person, m.personTrack.value(person));
            continue;
        }
        const QHash<QString, double> votes = votesOf(it.value());
        const QString winner = diareval::assignTrack(votes, majority);
        if (perTrack) {
            const bool leak = !winner.isEmpty() && winner != own;
            groupTrack.insert(it.key(), leak ? QStringLiteral("-") : own);
            if (leak)
                ref.notes << QStringLiteral("Speaker %1 on %2: louder on %3 (votes: %4) -> leakage, dropped")
                                 .arg(person, own, winner, votesText(votes));
        } else {
            groupTrack.insert(it.key(), winner);
            ref.notes << QStringLiteral("Person %1: votes: %2 -> %3")
                             .arg(person, votesText(votes), winner.isEmpty() ? QStringLiteral("ambiguous") : winner);
        }
    }
    QMap<QString, QString> personTrack;       // the excluded persons ("") for the loop below
    QMap<QString, QVector<diareval::RefToken>> personTokens;
    for (auto it = groupTrack.constBegin(); it != groupTrack.constEnd(); ++it)
        if (it.value().isEmpty()) {
            personTrack.insert(it.key().first, QString());
            personTokens[it.key().first] += groups.value(it.key());
        }

    // The tracks.
    QVector<diareval::Interval> excludedAll;
    for (auto it = personTrack.constBegin(); it != personTrack.constEnd(); ++it)
        if (it.value().isEmpty()) {
            for (const diareval::Interval& u : diareval::utterancesOf(personTokens.value(it.key()))) excludedAll.append(u);
            ref.notes << QStringLiteral("Person %1: no unambiguous track, excluded from scoring.").arg(it.key());
        }
    for (const auto& t : m.tracks) {
        if (!t.included) continue;
        diareval::TrackReference tr;
        tr.trackId = t.id;
        tr.file = t.file;
        tr.startOffsetMs = t.startOffsetMs;
        QVector<diareval::RefToken> mine;
        for (auto it = groupTrack.constBegin(); it != groupTrack.constEnd(); ++it)
            if (it.value() == t.id) mine += groups.value(it.key());
        tr.turns = diareval::turnsFromTokens(mine);
        tr.excluded = excludedAll;
        ref.tracks.append(tr);
    }
    const QByteArray json = QJsonDocument(diareval::referenceToJson(ref)).toJson(QJsonDocument::Indented);
    if (!outPath.isEmpty()) {
        QFile of(outPath);
        if (!of.open(QIODevice::WriteOnly) || of.write(json) != json.size()) {
            serr() << "ERROR: cannot write " << outPath << "\n";
            return 1;
        }
        for (const diareval::TrackReference& tr : ref.tracks)
            sout() << tr.trackId << ": " << diareval::speakersOf(tr.turns).size() << " speaker(s), "
                   << tr.turns.size() << " turn(s)\n";
        for (const QString& n : ref.notes) sout() << "note: " << n << "\n";
    } else {
        sout() << json;
    }
    sout().flush();
    return 0;
}

// ---------------------------------------------------------------------------------------------------
// diarize-eval

namespace {

const char* kEvalUsage =
    "Usage: diarize-eval <audio|meeting-folder> --ref <json|rttm> [--track <id>] [--threshold a,b,...]\n"
    "                    [--shift <ratio>] [--voice-models a,b] [--sets \"a;b;a+b\"] [--num-speakers <n>]\n"
    "                    [--min-cluster-size <n>] [--collar <s>] [--no-leakage] [--no-merge] [--merge-sec <s>] [--workers <n>]\n"
    "                    [--embed-threads <n>] [--threads <n>] [--rttm-out <file>] [--json]\n"
    "  One segmentation + one embedding pass (the union of the sets' models), then every (set x threshold)\n"
    "  clustering is scored against the reference. --sets: model sets separated by ';', models by '+'\n"
    "  (default: --voice-models fused). --threshold: default = the calibrated per-model default.\n";

struct EvalTrack {
    providers::DiarizeTrack track;
    const diareval::TrackReference* ref = nullptr;
    QVector<float> pcm;
    SegmentationResult seg;
    TrackSpans spans;
    int leakTurns = 0;
    double leakSec = 0;
    qint64 segMs = 0, embedMs = 0;
};

QJsonObject derJson(const diareval::DerResult& r)
{
    return QJsonObject{{"der", r.der()}, {"missed", r.missedRate()}, {"falseAlarm", r.falseAlarmRate()},
                       {"confusion", r.confusionRate()}, {"scoredSec", r.scored}, {"refSpeakers", r.refSpeakers},
                       {"hypSpeakers", r.hypSpeakers}, {"speakerCountError", r.speakerCountError()},
                       {"missedSec", r.missed}, {"falseAlarmSec", r.falseAlarm}, {"confusionSec", r.confusion}};
}

} // namespace

int runDiarizeEvalCommand(const QStringList& args)
{
    QString input, refPath, trackId, rttmOut, dumpPath;
    QString rttm;   // the hypothesis of the first (set, threshold), for --rttm-out
    QStringList voiceModels, thresholdsArg, setsArg;
    SegmenterOptions opts;
    opts.windowShiftRatio = LocalDiarizer::kDefaultWindowShift;
    OnnxSegmentationOptions ort;
    ClusteringOptions copts;
    diareval::DerOptions dopts;
    bool json = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&](bool* ok) { *ok = i + 1 < args.size(); return *ok ? args[++i] : QString(); };
        bool ok = true;
        if (a == "--ref") refPath = next(&ok);
        else if (a == "--track") trackId = next(&ok);
        else if (a == "--threshold") thresholdsArg = next(&ok).split(QLatin1Char(','), Qt::SkipEmptyParts);
        else if (a == "--shift") opts.windowShiftRatio = next(&ok).toDouble(&ok);
        else if (a == "--voice-models") voiceModels = next(&ok).split(QLatin1Char(','), Qt::SkipEmptyParts);
        else if (a == "--sets") setsArg = next(&ok).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (a == "--num-speakers") copts.numClusters = next(&ok).toInt(&ok);
        else if (a == "--min-cluster-size") copts.minClusterSize = next(&ok).toInt(&ok);
        else if (a == "--collar") dopts.collar = next(&ok).toDouble(&ok);
        else if (a == "--no-leakage") copts.suppressLeakage = false;
        else if (a == "--no-merge") copts.mergeSpanSec = 0;
        else if (a == "--merge-sec") copts.mergeSpanSec = next(&ok).toDouble(&ok);
        else if (a == "--workers") copts.embedWorkers = next(&ok).toInt(&ok);
        else if (a == "--embed-threads") copts.embedThreads = next(&ok).toInt(&ok);
        else if (a == "--threads") ort.intraOpThreads = next(&ok).toInt(&ok);
        else if (a == "--json") json = true;
        else if (a == "--rttm-out") rttmOut = next(&ok);
        else if (a == "--dump-embeddings") dumpPath = next(&ok);
        else if (a.startsWith("--") || !input.isEmpty()) ok = false;
        else input = a;
        if (!ok) { serr() << kEvalUsage; return 2; }
    }
    if (input.isEmpty() || refPath.isEmpty() || opts.windowShiftRatio <= 0 || opts.windowShiftRatio > 1) {
        serr() << kEvalUsage;
        return 2;
    }
    QVector<double> thresholds;
    for (const QString& t : std::as_const(thresholdsArg)) {
        bool ok = false;
        thresholds.append(t.toDouble(&ok));
        if (!ok) { serr() << kEvalUsage; return 2; }
    }
    if (thresholds.isEmpty()) thresholds.append(-1.0);

    // The reference.
    QFile rf(refPath);
    if (!rf.open(QIODevice::ReadOnly)) { serr() << "ERROR: cannot read " << refPath << "\n"; return 1; }
    const QByteArray refData = rf.readAll();
    QString error;
    const diareval::Reference ref = refData.trimmed().startsWith('{')
                                        ? diareval::parseReferenceJson(refData, &error)
                                        : diareval::parseRttm(QString::fromUtf8(refData), &error);
    if (ref.tracks.isEmpty()) { serr() << "ERROR: empty reference " << error << "\n"; return 1; }

    app::SettingsManager sm;
    const QString appDir = QCoreApplication::applicationDirPath();
    const auto media = media::FfmpegMediaTool::fromSettings(sm.settings().media);

    // The tracks.
    QVector<EvalTrack> tracks;
    if (QFileInfo(input).isDir()) {
        MeetingInfo m;
        if (!loadMeeting(input, &m, &error)) { serr() << "ERROR: " << error << "\n"; return 1; }
        for (const auto& t : m.tracks) {
            if (!t.included || (!trackId.isEmpty() && t.id != trackId)) continue;
            EvalTrack et;
            et.track.trackId = t.id;
            et.track.path = QDir(input).filePath(t.file);
            et.track.startOffsetMs = t.startOffsetMs;
            et.ref = ref.track(t.id);
            if (!et.ref) { serr() << "warning: track " << t.id << " is not in the reference; skipped\n"; continue; }
            tracks.append(et);
        }
    } else {
        EvalTrack et;
        et.ref = !trackId.isEmpty() ? ref.track(trackId) : (ref.tracks.size() == 1 ? &ref.tracks.first() : nullptr);
        if (!et.ref) { serr() << "ERROR: name the reference track with --track\n"; return 1; }
        et.track.trackId = et.ref->trackId;
        et.track.path = input;
        et.track.startOffsetMs = et.ref->startOffsetMs;
        tracks.append(et);
    }
    if (tracks.isEmpty()) { serr() << "ERROR: no track to evaluate\n"; return 1; }

    // The model sets.
    QStringList enabled = voiceModels.isEmpty() ? sm.settings().processing.voiceModels : voiceModels;
    QVector<QStringList> sets;
    for (const QString& s : std::as_const(setsArg)) {
        QStringList ids = s.split(QLatin1Char('+'), Qt::SkipEmptyParts);
        ids.sort();
        sets.append(ids);
    }
    QStringList allModels;
    for (const QStringList& s : std::as_const(sets))
        for (const QString& id : s)
            if (!allModels.contains(id)) allModels << id;
    if (sets.isEmpty()) allModels = enabled;
    // One embedder set per worker; each worker loads its own sessions (with --embed-threads intra-op threads).
    const VoiceEmbedderSet proto = LocalDiarizer::withThreads(
        VoiceEmbedderSet::fromSettings(allModels, sm.paths().modelsDir(), appDir), copts.embedThreads);
    if (!proto.ensureLoaded()) { serr() << "ERROR: no usable voice model (" << proto.lastError() << ")\n"; return 1; }
    const QStringList loaded = proto.loadedModelIds();
    if (sets.isEmpty()) sets.append(loaded);
    for (const QStringList& s : std::as_const(sets))
        for (const QString& id : s)
            if (!loaded.contains(id)) { serr() << "ERROR: voice model not available: " << id << "\n"; return 1; }
    const int workers = std::max(1, copts.embedWorkers);
    QVector<VoiceEmbedderSet> workerSets;
    workerSets.reserve(workers);
    QVector<LocalDiarizer::SpanEmbedder> embedFns;
    for (int w = 0; w < workers; ++w) workerSets.append(w == 0 ? proto : LocalDiarizer::withThreads(proto, copts.embedThreads));
    for (int w = 0; w < workers; ++w) {
        const VoiceEmbedderSet* set = &workerSets[w];
        embedFns.append([set, loaded](const QVector<float>& pcm) { return set->embedPcmWith(pcm, loaded); });
    }

    // Segmentation.
    const std::optional<VoiceModelSpec> segSpec = VoiceModelRegistry::spec(VoiceModelRegistry::defaultSegmentationModelId());
    std::unique_ptr<PyannoteSegmenter> segmenter =
        segSpec ? PyannoteSegmenter::open(*segSpec, sm.paths().modelsDir(), appDir, opts, ort, &error) : nullptr;
    if (!segmenter) { serr() << "ERROR: " << error << "\n"; return 1; }
    QElapsedTimer clock;
    double audioSec = 0;
    for (EvalTrack& et : tracks) {
        et.pcm = decode16k(*media, et.track.path, &error);
        if (et.pcm.isEmpty()) { serr() << "ERROR: cannot decode " << et.track.path << ": " << error << "\n"; return 1; }
        clock.start();
        const auto r = segmenter->segment(et.pcm, {}, nullptr, &error);
        if (!r) { serr() << "ERROR: " << error << "\n"; return 1; }
        et.seg = *r;
        et.segMs = clock.elapsed();
        audioSec += et.seg.durationSec();
    }
    // Leakage.
    QVector<QVector<leakage::Turn>> suppress(tracks.size());
    if (copts.suppressLeakage && tracks.size() >= 2) {
        QVector<providers::DiarizeTrack> ts;
        QVector<const SegmentationResult*> sp;
        QVector<const QVector<float>*> pp;
        for (const EvalTrack& et : tracks) { ts.append(et.track); sp.append(&et.seg); pp.append(&et.pcm); }
        const auto leaks = LocalDiarizer::detectLeakage(ts, sp, pp, copts.leakage);
        for (int k = 0; k < tracks.size(); ++k)
            for (const leakage::Leak& l : leaks[k]) {
                suppress[k].append(l.span);
                ++tracks[k].leakTurns;
                tracks[k].leakSec += l.span.end - l.span.start;
            }
    }
    // Embedding (once per track).
    double embedSecTotal = 0;
    for (int k = 0; k < tracks.size(); ++k) {
        EvalTrack& et = tracks[k];
        clock.start();
        et.spans = LocalDiarizer::embedTrack(et.seg, et.pcm, copts, suppress[k], embedFns);
        et.embedMs = clock.elapsed();
        embedSecTotal += et.embedMs / 1000.0;
    }

    // --dump-embeddings (a developer aid): every embedded span's time extent and per-model vectors.
    if (!dumpPath.isEmpty()) {
        QJsonArray all;
        for (const EvalTrack& et : tracks) {
            const double step = et.seg.info.frameStepSec();
            for (const TrackSpans::Span& sp : et.spans.spans) {
                if (sp.set.isEmpty() || sp.runs.isEmpty()) continue;
                QJsonArray runs;
                for (const auto& r : sp.runs)
                    runs.append(QJsonArray{et.seg.info.frameTimeSec(r.first) + et.track.startOffsetMs / 1000.0,
                                           et.seg.info.frameTimeSec(r.second) + et.track.startOffsetMs / 1000.0});
                QJsonObject vecs;
                for (auto it = sp.set.constBegin(); it != sp.set.constEnd(); ++it) {
                    QJsonArray v;
                    for (float x : it.value()) v.append(double(x));
                    vecs[it.key()] = v;
                }
                all.append(QJsonObject{{"trackId", et.track.trackId}, {"stitched", sp.stitched}, {"runs", runs},
                                       {"step", step}, {"vectors", vecs}});
            }
        }
        QFile of(dumpPath);
        if (of.open(QIODevice::WriteOnly)) of.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
    }

    // Every (set x threshold) clustering, scored.
    QJsonArray results;
    QJsonArray trackInfo;
    for (int k = 0; k < tracks.size(); ++k) {
        const EvalTrack& et = tracks[k];
        trackInfo.append(QJsonObject{{"trackId", et.track.trackId}, {"durationSec", et.seg.durationSec()},
                                     {"segmentMs", et.segMs}, {"embedMs", et.embedMs},
                                     {"embedRtf", et.seg.durationSec() > 0 ? et.embedMs / 1000.0 / et.seg.durationSec() : 0.0},
                                     {"spans", et.spans.pairs}, {"merged", int(et.spans.spans.size())},
                                     {"embeddedSec", et.spans.embeddedSec}, {"leakTurns", et.leakTurns},
                                     {"leakSec", et.leakSec}, {"refSpeakers", int(diareval::speakersOf(et.ref->turns).size())}});
    }
    if (!json) {
        sout() << "Audio: " << QString::number(audioSec, 'f', 1) << " s in " << tracks.size() << " track(s); shift "
               << opts.windowShiftRatio << ", merge " << copts.mergeSpanSec << " s, " << workers << " worker(s) x "
               << copts.embedThreads << " thread(s), leakage " << (copts.suppressLeakage ? "on" : "off") << "\n";
        for (const EvalTrack& et : tracks)
            sout() << "  " << et.track.trackId << ": " << QString::number(et.seg.durationSec(), 'f', 1) << " s, segment "
                   << et.segMs << " ms, embed " << et.embedMs << " ms (" << et.spans.pairs << " spans -> "
                   << et.spans.spans.size() << " merged, " << QString::number(et.spans.embeddedSec, 'f', 0)
                   << " s audio), leaked turns " << et.leakTurns << " (" << QString::number(et.leakSec, 'f', 1) << " s)\n";
        sout() << "models | threshold | DER | missed | FA | confusion | speakers hyp/ref per track\n";
    }
    for (const QStringList& set : std::as_const(sets))
        for (double thr : std::as_const(thresholds)) {
            ClusteringOptions c = copts;
            c.threshold = thr;
            diareval::DerResult total;
            QJsonArray per;
            QStringList spk;
            double usedThr = 0;
            for (const EvalTrack& et : tracks) {
                providers::DiarizeResult out;
                const TrackClusterStats st = LocalDiarizer::clusterSpans(et.track, et.seg, et.spans, opts, c, set, &out);
                usedThr = st.threshold;
                QVector<diareval::EvalTurn> hyp;
                for (const providers::SpeakerTurn& t : out.turns)
                    hyp.append({t.startMs / 1000.0, t.endMs / 1000.0, QString::number(t.speakerIndex)});
                if (results.isEmpty()) rttm += diareval::toRttm(et.track.trackId, hyp);
                const diareval::DerResult r = diareval::computeDer(et.ref->turns, hyp, dopts, et.ref->excluded);
                total += r;
                QJsonObject o = derJson(r);
                o["trackId"] = et.track.trackId;
                o["clusters"] = st.clusters;
                o["embedded"] = st.embedded;
                per.append(o);
                spk << QStringLiteral("%1/%2").arg(r.hypSpeakers).arg(r.refSpeakers);
            }
            QJsonObject row = derJson(total);
            row["models"] = set.join(QLatin1Char('+'));
            row["threshold"] = usedThr;
            row["tracks"] = per;
            results.append(row);
            if (!json)
                sout() << set.join(QLatin1Char('+')) << " | " << usedThr << " | "
                       << QString::number(total.der() * 100, 'f', 1) << "% | "
                       << QString::number(total.missedRate() * 100, 'f', 1) << "% | "
                       << QString::number(total.falseAlarmRate() * 100, 'f', 1) << "% | "
                       << QString::number(total.confusionRate() * 100, 'f', 1) << "% | " << spk.join(QLatin1Char(' ')) << "\n";
        }
    if (!rttmOut.isEmpty()) {
        QFile of(rttmOut);
        if (of.open(QIODevice::WriteOnly)) of.write(rttm.toUtf8());
    }
    const double rss = peakRssMb();
    if (json) {
        QJsonObject o;
        o["input"] = input;
        o["audioSec"] = audioSec;
        o["shift"] = opts.windowShiftRatio;
        o["mergeSpanSec"] = copts.mergeSpanSec;
        o["workers"] = workers;
        o["embedThreads"] = copts.embedThreads;
        o["leakage"] = copts.suppressLeakage;
        o["collar"] = dopts.collar;
        o["embedSec"] = embedSecTotal;
        o["embedRtf"] = audioSec > 0 ? embedSecTotal / audioSec : 0.0;
        o["peakRssMb"] = rss;
        o["tracks"] = trackInfo;
        o["results"] = results;
        sout() << QJsonDocument(o).toJson(QJsonDocument::Compact) << "\n";
    } else {
        sout() << "Embedding: " << QString::number(embedSecTotal, 'f', 1) << " s (RTF "
               << QString::number(audioSec > 0 ? embedSecTotal / audioSec : 0.0, 'f', 4) << "), peak RSS "
               << QString::number(rss, 'f', 0) << " MB\n";
    }
    sout().flush();
    return 0;
}

} // namespace tanara::cli
