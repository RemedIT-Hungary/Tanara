// tanara-cli remote-lines. All output is English, no tr().
#include "RemoteLinesCommand.h"
#include "CommandRegistry.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/editing/review/RemoteLineClusters.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/participants/ParticipantSample.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/library/store/VoiceFusion.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

namespace tanara::cli {

namespace {

using editing::SpeakerEditSession;
namespace rl = editing::remotelines;

const QStringList kAudioSuffixes{"ogg", "mp3", "wav", "flac", "opus", "m4a", "aac", "webm"};

// The meeting folder copied into <root>/<name>: the audio files are linked (read-only use), the rest copied.
bool copyMeetingFolder(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to)) return false;
    QDir src(from);
    for (const QFileInfo& fi : src.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        const QString dst = QDir(to).filePath(fi.fileName());
        const bool audio = kAudioSuffixes.contains(fi.suffix().toLower());
        if (audio ? !QFile::link(fi.absoluteFilePath(), dst) : !QFile::copy(fi.absoluteFilePath(), dst)) return false;
        QFile::setPermissions(dst, QFile::permissions(dst) | QFileDevice::WriteOwner);
    }
    return true;
}

struct TruthLine {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString key;        // the true speaker key ("participant:N")
    bool remote = false;
};

struct Truth {
    QHash<QString, PersonId> personOf;    // truth key -> person
    QVector<TruthLine> lines;
    bool ok = false;
};

Truth loadTruth(const QString& path)
{
    Truth t;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return t;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root.value("participants").toArray()) {
        const QJsonObject o = v.toObject();
        t.personOf.insert(o.value("key").toString(), PersonId::fromString(o.value("personId").toString()));
    }
    for (const QJsonValue& v : root.value("lines").toArray()) {
        const QJsonObject o = v.toObject();
        TruthLine l;
        l.startMs = qint64(o.value("startMs").toDouble());
        l.endMs = qint64(o.value("endMs").toDouble());
        l.key = o.value("truth").toString();
        l.remote = o.value("side").toString() == QLatin1String("loopback");
        t.lines.append(l);
    }
    t.ok = !t.lines.isEmpty();
    return t;
}

// The embedder set of the enabled voice models (the app's settings).
processing::VoiceEmbedderSet embedderSet(const app::SettingsManager& sm, QString* error)
{
    QVector<processing::VoiceModelEntry> entries;
    const QString modelsDir = sm.paths().modelsDir();
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const processing::VoiceModelSpec& spec : processing::VoiceModelRegistry::active(sm.enabledVoiceModels(), modelsDir, appDir))
        entries.append({spec, processing::VoiceModelRegistry::resolvePath(spec, modelsDir, appDir)});
    if (entries.isEmpty() && error) *error = QStringLiteral("no voice model installed");
    return processing::VoiceEmbedderSet(entries);
}

void embedWindow(const TranscriptLine& l, qint64* s, qint64* e)
{
    *s = l.startMs;
    *e = l.endMs;
    if (*e - *s > editing::speakeredit::kMaxEmbedMs) {
        *s = (l.startMs + l.endMs) / 2 - editing::speakeredit::kMaxEmbedMs / 2;
        *e = *s + editing::speakeredit::kMaxEmbedMs;
    }
}

// Fills the meeting folder's line-embedding cache (the lines >= kMinEmbedMs still missing a vector).
// fromTracks: every line from the track of its side (Remote -> loopback, Local -> mic, else the mixdown); fresh:
// the existing cache is ignored (the evaluation of the audio source). Returns the number of lines embedded, -1 on error.
int fillEmbeddings(const Meeting& m, const QVector<TranscriptLine>& lines, const processing::VoiceEmbedderSet& set,
                   media::MediaToolPtr media, bool fromTracks, bool fresh, const QVector<editing::Side>& sides,
                   QString* error)
{
    const QString fp = editing::speakeredit::transcriptFingerprint(lines);
    library::UtteranceEmbeddingCache cache = fresh ? library::UtteranceEmbeddingCache{}
                                                        : library::UtteranceEmbeddingCache::load(m.folder, fp);
    cache.fingerprint = fp;
    const QStringList models = set.modelIds();
    auto factory = processing::voiceUtteranceEmbedderFactory(set, media);
    if (!factory) { *error = QStringLiteral("no voice model"); return -1; }

    // Audio sources: the mixdown, and (fromTracks) the first file of each side's track.
    struct Source { QString path; qint64 offsetMs = 0; std::shared_ptr<providers::IUtteranceEmbedder> emb; };
    QHash<QString, Source> sources;
    auto open = [&](const QString& name, const QString& path, qint64 offset) -> Source* {
        auto it = sources.find(name);
        if (it != sources.end()) return it->emb ? &it.value() : nullptr;
        Source s;
        s.path = path;
        s.offsetMs = offset;
        s.emb = std::shared_ptr<providers::IUtteranceEmbedder>(factory());
        if (!s.emb || !s.emb->open(path)) {
            *error = s.emb ? s.emb->lastError() : QStringLiteral("cannot open %1").arg(path);
            s.emb.reset();
        }
        it = sources.insert(name, std::move(s));
        return it->emb ? &it.value() : nullptr;
    };
    const Track* micTrack = nullptr;
    const Track* loopTrack = nullptr;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        if (t.kind == TrackKind::Mic && !micTrack) micTrack = &t;
        if (t.kind == TrackKind::Loopback && !loopTrack) loopTrack = &t;
    }
    int done = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const TranscriptLine& l = lines[i];
        if (l.endMs - l.startMs < editing::speakeredit::kMinEmbedMs) continue;
        QStringList missing;
        for (const QString& id : models)
            if (!cache.has(id, l.id)) missing << id;
        if (missing.isEmpty()) continue;
        qint64 s = 0, e = 0;
        embedWindow(l, &s, &e);
        Source* src = nullptr;
        const editing::Side side = sides.value(i, editing::Side::Unknown);
        const Track* track = !fromTracks ? nullptr
                           : side == editing::Side::Remote ? loopTrack
                           : side == editing::Side::Local ? micTrack : nullptr;
        if (track) {
            src = open(track->id, QDir(m.folder).filePath(track->file), track->startOffsetMs);
            if (src) { s -= src->offsetMs; e -= src->offsetMs; }
        } else {
            src = open(QStringLiteral("mixdown"), editing::speakeredit::mixdownPath(m), 0);
        }
        if (!src) return -1;
        const EmbeddingSet got = src->emb->embedAll(std::max<qint64>(0, s), std::max<qint64>(0, e), missing);
        for (const QString& id : missing) cache.set(id, l.id, got.value(id));
        ++done;
        if (done % 100 == 0) { err() << "  embedded " << done << " lines\n"; err().flush(); }
    }
    if (!cache.save(m.folder)) { *error = QStringLiteral("cannot write the embedding cache"); return -1; }
    return done;
}

struct Score {
    int right = 0, wrong = 0;
    qint64 rightMs = 0, wrongMs = 0;
    void add(bool ok, qint64 ms) { if (ok) { ++right; rightMs += ms; } else { ++wrong; wrongMs += ms; } }
    QJsonObject json() const
    {
        const int n = right + wrong;
        const qint64 ms = rightMs + wrongMs;
        return {{"right", right}, {"wrong", wrong}, {"rightSec", rightMs / 1000.0}, {"wrongSec", wrongMs / 1000.0},
                {"rightShare", n > 0 ? double(right) / n : 0.0}, {"rightShareSec", ms > 0 ? double(rightMs) / ms : 0.0}};
    }
    QString text() const
    {
        const int n = right + wrong;
        const qint64 ms = rightMs + wrongMs;
        return QStringLiteral("%1/%2 right (%3%), %4 s / %5 s (%6%)")
            .arg(right).arg(n).arg(n > 0 ? 100.0 * right / n : 0.0, 0, 'f', 1)
            .arg(rightMs / 1000.0, 0, 'f', 0).arg(ms / 1000.0, 0, 'f', 0)
            .arg(ms > 0 ? 100.0 * rightMs / ms : 0.0, 0, 'f', 1);
    }
};

} // namespace

int runRemoteLinesCommand(const QStringList& args)
{
    QString folder, truthPath, cacheFile, audio = QStringLiteral("mixdown");
    bool apply = false, json = false, strip = false, excludePrints = false, singleTrack = false, approval = false;
    int seeds = 0;
    quint32 seedRandom = 1;
    qint64 afterMs = 40 * 60 * 1000;
    rl::Options opt;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        auto next = [&]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (a == "--apply") apply = true;
        else if (a == "--json") json = true;
        else if (a == "--truth") truthPath = next();
        else if (a == "--strip-utterances") strip = true;
        else if (a == "--seeds") seeds = next().toInt();
        else if (a == "--seed-random") seedRandom = next().toUInt();
        else if (a == "--exclude-meeting-prints") excludePrints = true;
        else if (a == "--audio") audio = next();
        else if (a == "--single-track") singleTrack = true;
        else if (a == "--merge") opt.mergeThreshold = next().toDouble();
        else if (a == "--linkage") opt.linkage = next() == QLatin1String("centroid") ? rl::Linkage::Centroid : rl::Linkage::Average;
        else if (a == "--seed-min-ms") opt.seedMinMs = next().toLongLong();
        else if (a == "--min-cluster-ms") opt.minClusterMs = next().toLongLong();
        else if (a == "--nearest-margin") opt.nearestMargin = next().toDouble();
        else if (a == "--after-ms") afterMs = next().toLongLong();
        else if (a == "--no-smooth") opt.smoothTurns = false;
        else if (a == "--no-keep-weak") opt.keepWeakShort = false;
        else if (a == "--approval") approval = true;
        else if (a == "--weak-margin") opt.weakMargin = next().toDouble();
        else if (a == "--turn-gap-ms") opt.turnGapMs = next().toLongLong();
        else if (a == "--cache-file") cacheFile = next();
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    const bool evaluation = !truthPath.isEmpty() || strip || seeds > 0 || audio != QLatin1String("mixdown") || singleTrack
                            || !cacheFile.isEmpty() || approval;
    if (folder.isEmpty() || (audio != "mixdown" && audio != "tracks") || (apply && evaluation)) {
        err() << "Usage: remote-lines <meeting-folder> [--apply] [--json]\n"
                 "       remote-lines <meeting-folder> --truth <file> [--strip-utterances] [--seeds N] [--seed-random S]\n"
                 "                    [--exclude-meeting-prints] [--audio mixdown|tracks] [--single-track]\n"
                 "                    [--merge 0.30] [--seed-min-ms 2000] [--min-cluster-ms 15000] [--nearest-margin 0.10]\n"
                 "                    [--after-ms 2400000] [--approval] [--cache-file F] [--json]\n"
                 "Dry run by default (on a temporary copy); --apply writes the meeting (not with the evaluation options).\n";
        return 2;
    }
    folder = QFileInfo(folder).absoluteFilePath();
    Meeting original = library::MeetingRepository::readMeetingFolder(folder);
    if (original.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    Truth truth;
    if (!truthPath.isEmpty()) {
        truth = loadTruth(truthPath);
        if (!truth.ok) { err() << "Cannot read the truth file: " << truthPath << "\n"; return 1; }
    }

    // ---- the workspace: a temporary copy (dry run / evaluation) or the meeting itself (--apply) ----
    QTemporaryDir tmp;
    if (!tmp.isValid()) { err() << "Cannot create a temporary folder\n"; return 1; }
    QString root, work;
    if (apply) {
        root = library::MeetingRepository::rootOf(folder);
        work = folder;
    } else {
        root = QDir(tmp.path()).filePath("root");
        work = QDir(root).filePath(QFileInfo(folder).fileName());
        if (!copyMeetingFolder(folder, work)) { err() << "Cannot copy the meeting folder\n"; return 1; }
    }
    library::MeetingRepository repo(root, QDir(tmp.path()).filePath("meta"));
    repo.reindex();
    Meeting m = repo.load(original.id);
    if (m.id.isEmpty()) { err() << "Cannot load the meeting from " << root << "\n"; return 1; }
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(work);
    if (lines.isEmpty()) { err() << "No transcript (transcript.segments.json) in " << folder << "\n"; return 1; }

    // Evaluation: the manual line decisions withheld, then (optionally) N random truth lines as confirmed seeds.
    QHash<QString, QString> truthKeyOf;   // utterance id -> truth key
    QHash<QString, TruthLine> truthOf;
    if (truth.ok) {
        // By time overlap: a line (possibly cut at a side change since the truth was made) takes the truth line
        // covering most of it, if that covers at least half of it.
        for (const TranscriptLine& l : lines) {
            const qint64 dur = std::max<qint64>(1, l.endMs - l.startMs);
            const TruthLine* best = nullptr;
            qint64 bestOv = 0;
            for (const TruthLine& t : std::as_const(truth.lines)) {
                const qint64 ov = std::min(l.endMs, t.endMs) - std::max(l.startMs, t.startMs);
                if (ov > bestOv) { bestOv = ov; best = &t; }
            }
            if (!best || 2 * bestOv < dur) continue;
            truthKeyOf.insert(l.id, best->key);
            truthOf.insert(l.id, *best);
        }
    }
    QSet<QString> seedIds;
    if (strip || seeds > 0) {
        SpeakerOverlay ov = editing::speakeredit::loadOverlay(work);
        if (strip) {
            ov.utterances.clear();
            ov.remoteAssignedAt.clear();
        }
        if (seeds > 0) {
            QStringList ids = truthKeyOf.keys();
            std::sort(ids.begin(), ids.end());
            QRandomGenerator rng(seedRandom);
            std::shuffle(ids.begin(), ids.end(), rng);
            for (int k = 0; k < ids.size() && seedIds.size() < seeds; ++k) {
                OverlayUtterance& u = ov.utterances[ids[k]];
                u.speaker = truthKeyOf.value(ids[k]);
                u.corrected = true;
                seedIds.insert(ids[k]);
            }
        }
        editing::speakeredit::saveOverlay(work, ov);
    }

    app::SettingsManager sm;
    library::PeopleStore people;
    library::VoiceprintStore prints;
    media::MediaToolPtr media = media::FfmpegMediaTool::fromSettings(sm.settings().media);

    QElapsedTimer timer;
    timer.start();
    const MeetingActivity act = processing::loadOrComputeSideActivity(m, *media);
    const qint64 activityMs = timer.elapsed();
    const QVector<editing::Side> sideOf = editing::sides::classifyLineSides(act, lines);

    // ---- line embeddings ----
    QString error;
    const processing::VoiceEmbedderSet set = embedderSet(sm, &error);
    if (set.isEmpty()) { err() << "Cannot embed: " << error << "\n"; return 1; }
    timer.restart();
    err() << "Line embeddings (" << set.modelIds().join(",") << ", " << audio << ")…\n";
    err().flush();
    // --cache-file: the evaluation's line vectors kept outside the copy (another audio source / model set is
    // computed once): taken into the copy before, written back after.
    const QString cachePath = library::UtteranceEmbeddingCache::filePath(work);
    const bool fromTracks = audio == QLatin1String("tracks");
    if (!cacheFile.isEmpty()) {
        QFile::remove(cachePath);   // a missing --cache-file: every vector is computed (measures the full run)
        if (QFileInfo::exists(cacheFile)) QFile::copy(cacheFile, cachePath);
    }
    const bool fresh = fromTracks && cacheFile.isEmpty();
    const int embedded = fillEmbeddings(m, lines, set, media, fromTracks, fresh, sideOf, &error);
    if (!cacheFile.isEmpty() && embedded > 0) {
        QFile::remove(cacheFile);
        QFile::copy(cachePath, cacheFile);
    }
    if (embedded < 0) { err() << "Cannot embed: " << error << "\n"; return 1; }
    const qint64 embedMs = timer.elapsed();

    // ---- the plan ----
    SpeakerEditSession session(&repo, &people, &prints, m.id);
    session.setSelfPerson(SelfPerson{people.selfId(), sm.settings().recording.userSpeakerName.trimmed()});
    session.setVoiceModelIds(set.modelIds());
    SpeakerEditSession::RemoteAssignContext ctx;
    ctx.analysisClusters = processing::ParticipantAnalysisFile::load(work).clusters;
    ctx.participants = m.participants;
    ctx.activity = act;
    ctx.options = opt;
    ctx.singleTrack = singleTrack || rl::kClusterSingleTrack;
    if (excludePrints) ctx.excludePrintsOfMeeting = m.id;
    MeetingActivity none;
    if (singleTrack) ctx.activity = none;   // the side knowledge withheld
    // --approval: first the pipeline's participant-approval bindings (as after a transcription: the local side's
    // ownership, the track split), applied as automatic; the evaluation's baseline is then that state.
    if (approval && m.approval && !m.approval->skipped && !m.approval->solo) {
        QStringList accepted;
        for (const Participant& p : m.participants)
            if (p.approved) accepted << p.id;
        const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(work);
        const SpeakerOverlay ov = editing::speakeredit::loadOverlayFor(work, lines);
        QVector<SpeakerBinding> bindings = processing::participants::approvalBindings(
            m.participants, accepted, lines, editing::participantsample::withMerges(file.clusters, m.participants),
            act.isEmpty() ? nullptr : &act, &m, people.selfId(), &ov);
        for (SpeakerBinding& b : bindings) b.automatic = true;
        session.applyBindings(bindings, QStringLiteral("approval"));
    }
    timer.restart();
    const SpeakerEditSession::RemoteAssignPlan plan = session.planRemoteAssignment(ctx);
    const qint64 planMs = timer.elapsed();

    QJsonObject root_;
    root_["meetingId"] = m.id;
    root_["dryRun"] = !apply;
    root_["models"] = QJsonArray::fromStringList(set.modelIds());
    root_["audio"] = audio;
    root_["timing"] = QJsonObject{{"activityMs", double(activityMs)}, {"embedMs", double(embedMs)},
                                  {"embeddedLines", embedded}, {"assignMs", double(planMs)}};
    root_["thresholds"] = QJsonObject{{"merge", opt.mergeThreshold}, {"seedMinMs", double(opt.seedMinMs)},
                                      {"minClusterMs", double(opt.minClusterMs)}, {"nearestMargin", opt.nearestMargin}};
    editing::SideCounts sc;
    for (editing::Side s : sideOf) sc.add(s);
    root_["sides"] = QJsonObject{{"local", sc.local}, {"remote", sc.remote}, {"mixed", sc.mixed}, {"unknown", sc.unknown}};
    root_["ran"] = plan.ran;
    if (!plan.ran) root_["skipped"] = plan.skipped;

    // Per line: the person of a speaker key in the session (the plan's "+person:<uuid>" placeholder too).
    auto personOfKey = [&](const QString& key) -> PersonId {
        if (key.startsWith(QLatin1String("+person:"))) return PersonId::fromString(key.mid(8));
        if (key.startsWith(QLatin1Char('+'))) return {};
        return session.speaker(key).personId;
    };

    QJsonArray clustersJ;
    QHash<QString, int> basisCount;
    if (plan.ran) {
        for (int k = 0; k < plan.result.clusters.size(); ++k) {
            const rl::Cluster& c = plan.result.clusters[k];
            QJsonObject o{{"index", k}, {"lines", int(c.lines.size())}, {"seedSec", c.seedMs / 1000.0},
                          {"locked", c.lockedLines}, {"target", c.target}, {"speaker", plan.targetKey.value(c.target)},
                          {"bind", c.bindBasis}, {"windowSec", c.windowMs / 1000.0}};
            if (!std::isnan(c.windowShare)) o["windowShare"] = c.windowShare;
            if (!std::isnan(c.printScore)) o["printScore"] = c.printScore;
            if (truth.ok) {
                QMap<QString, int> per;
                int n = 0;
                for (int i : c.lines) {
                    const QString t = truthKeyOf.value(lines[i].id);
                    if (t.isEmpty()) continue;
                    ++per[t];
                    ++n;
                }
                int best = 0;
                QString bestKey;
                for (auto it = per.cbegin(); it != per.cend(); ++it)
                    if (it.value() > best) { best = it.value(); bestKey = it.key(); }
                o["truthLines"] = n;
                o["truthMajority"] = bestKey;
                o["truthMajorityLines"] = best;
            }
            clustersJ.append(o);
        }
        for (const rl::LineDecision& d : plan.result.lines) ++basisCount[rl::basisName(d.basis)];
    }
    root_["clusters"] = clustersJ;
    QJsonObject bc;
    for (auto it = basisCount.cbegin(); it != basisCount.cend(); ++it) bc[it.key()] = it.value();
    root_["summary"] = QJsonObject{{"seeds", plan.result.seeds}, {"clusters", int(plan.result.clusters.size())},
                                   {"dissolved", plan.result.dissolved}, {"anonymous", plan.result.anonymous},
                                   {"changed", plan.changed}, {"weak", plan.weak},
                                   {"newParticipants", plan.newParticipants}, {"basis", bc},
                                   {"singleTrack", plan.singleTrack}};

    // The distribution of the remote lines after afterMs (where the provider may have merged voices).
    if (plan.ran) {
        QMap<QString, QPair<int, qint64>> after, rawAfter;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].startMs < afterMs || plan.input[i].side != editing::Side::Remote) continue;
            const qint64 ms = lines[i].endMs - lines[i].startMs;
            auto& a = after[plan.newKey[i]];
            ++a.first; a.second += ms;
            auto& r = rawAfter[lines[i].rawLabel];
            ++r.first; r.second += ms;
        }
        QJsonObject aj, rj;
        for (auto it = after.cbegin(); it != after.cend(); ++it)
            aj[it.key()] = QJsonObject{{"lines", it->first}, {"sec", it->second / 1000.0}};
        for (auto it = rawAfter.cbegin(); it != rawAfter.cend(); ++it)
            rj[it.key()] = QJsonObject{{"lines", it->first}, {"sec", it->second / 1000.0}};
        root_["afterMinute"] = double(afterMs) / 60000.0;
        root_["remoteAfter"] = aj;
        root_["rawAfter"] = rj;
    }

    // ---- evaluation against the truth ----
    if (truth.ok) {
        // Which truth people are reachable by name after the run (a speaker / planned participant with that person).
        QSet<PersonId> namedAfter;
        for (int i = 0; i < lines.size(); ++i) {
            const PersonId p = personOfKey(plan.ran ? plan.newKey[i] : session.utteranceAt(i).speakerKey);
            if (!p.isNull()) namedAfter.insert(p);
        }
        auto evaluate = [&](const QVector<QString>& keyOf) {
            // An anonymous speaker counts as its majority truth person when that person has no named speaker.
            QHash<QString, QHash<PersonId, int>> anonVotes;
            for (int i = 0; i < lines.size(); ++i) {
                const QString t = truthKeyOf.value(lines[i].id);
                if (t.isEmpty() || !personOfKey(keyOf[i]).isNull()) continue;
                ++anonVotes[keyOf[i]][truth.personOf.value(t)];
            }
            QHash<QString, PersonId> anonAs;
            for (auto it = anonVotes.cbegin(); it != anonVotes.cend(); ++it) {
                PersonId best;
                int bv = 0;
                for (auto jt = it->cbegin(); jt != it->cend(); ++jt)
                    if (jt.value() > bv) { bv = jt.value(); best = jt.key(); }
                if (!namedAfter.contains(best)) anonAs.insert(it.key(), best);
            }
            QJsonObject o;
            Score strictVec, strictNoVec, lenientVec, lenientNoVec, local;
            for (int i = 0; i < lines.size(); ++i) {
                const QString t = truthKeyOf.value(lines[i].id);
                if (t.isEmpty() || seedIds.contains(lines[i].id)) continue;
                const TruthLine& tl = truthOf[lines[i].id];
                const PersonId want = truth.personOf.value(t);
                const PersonId got = personOfKey(keyOf[i]);
                const qint64 ms = lines[i].endMs - lines[i].startMs;
                const bool strict = !got.isNull() && got == want;
                const bool lenient = strict || (got.isNull() && anonAs.value(keyOf[i]) == want && !want.isNull());
                if (!tl.remote) { local.add(strict, ms); continue; }
                const bool vec = plan.hasVector.value(i);
                (vec ? strictVec : strictNoVec).add(strict, ms);
                (vec ? lenientVec : lenientNoVec).add(lenient, ms);
            }
            Score strictAll = strictVec, lenientAll = lenientVec;
            strictAll.right += strictNoVec.right; strictAll.wrong += strictNoVec.wrong;
            strictAll.rightMs += strictNoVec.rightMs; strictAll.wrongMs += strictNoVec.wrongMs;
            lenientAll.right += lenientNoVec.right; lenientAll.wrong += lenientNoVec.wrong;
            lenientAll.rightMs += lenientNoVec.rightMs; lenientAll.wrongMs += lenientNoVec.wrongMs;
            o["remote"] = strictAll.json();
            o["remoteWithVector"] = strictVec.json();
            o["remoteNoVector"] = strictNoVec.json();
            o["remoteAnonByMajority"] = lenientAll.json();
            o["remoteWithVectorAnonByMajority"] = lenientVec.json();
            o["remoteNoVectorAnonByMajority"] = lenientNoVec.json();
            o["local"] = local.json();
            QJsonObject text{{"remote", strictAll.text()}, {"remoteWithVector", strictVec.text()},
                             {"remoteNoVector", strictNoVec.text()}, {"remoteAnonByMajority", lenientAll.text()},
                             {"remoteWithVectorAnonByMajority", lenientVec.text()},
                             {"remoteNoVectorAnonByMajority", lenientNoVec.text()}, {"local", local.text()}};
            o["text"] = text;
            return o;
        };
        QVector<QString> before(lines.size()), after(lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            before[i] = plan.ran ? plan.currentKey[i] : session.utteranceAt(i).speakerKey;
            after[i] = plan.ran ? plan.newKey[i] : before[i];
        }
        // Cluster purity against the truth (seed lines of the clusters with a truth).
        int pureN = 0, pureRight = 0;
        for (const QJsonValue& v : std::as_const(clustersJ)) {
            pureN += v.toObject().value("truthLines").toInt();
            pureRight += v.toObject().value("truthMajorityLines").toInt();
        }
        // Separability of the truth voices in the line vectors: the mean cosine within / between the truth
        // speakers (lines with a vector and at least seedMinMs).
        const library::UtteranceEmbeddingCache cache =
            library::UtteranceEmbeddingCache::load(work, editing::speakeredit::transcriptFingerprint(lines));
        QVector<QPair<QString, QVector<float>>> tv;
        for (int i = 0; i < lines.size(); ++i) {
            const QString t = truthKeyOf.value(lines[i].id);
            if (t.isEmpty() || lines[i].endMs - lines[i].startMs < opt.seedMinMs) continue;
            const QVector<float> v = library::fusion::fuse(cache.setFor(lines[i].id, set.modelIds()), set.modelIds());
            if (!v.isEmpty()) tv.append({t, v});
        }
        QMap<QString, QPair<double, int>> pairSim;
        for (int a = 0; a < tv.size(); ++a)
            for (int b = a + 1; b < tv.size(); ++b) {
                QString ka = tv[a].first, kb = tv[b].first;
                if (kb < ka) std::swap(ka, kb);
                auto& ps = pairSim[ka + QStringLiteral(" | ") + kb];
                ps.first += library::VoiceprintStore::cosineSimilarity(tv[a].second, tv[b].second);
                ++ps.second;
            }
        QJsonObject sep;
        for (auto it = pairSim.cbegin(); it != pairSim.cend(); ++it)
            sep[it.key()] = QJsonObject{{"meanCos", it->first / std::max(1, it->second)}, {"pairs", it->second}};
        root_["truthSeparability"] = sep;
        // The ceiling of a voice-only assignment: each truth line (with a vector) classified by the nearest truth
        // centroid built WITHOUT it (leave-one-out, duration-weighted): what a perfect clustering + binding could reach.
        {
            QHash<QString, QVector<double>> sum;
            for (const auto& [k, v] : std::as_const(tv)) {
                QVector<double>& acc = sum[k];
                if (acc.isEmpty()) acc.fill(0.0, v.size());
                for (int d = 0; d < v.size(); ++d) acc[d] += v[d];
            }
            int ok = 0, all = 0, okRemote = 0, allRemote = 0;
            int idx = 0;
            for (int i = 0; i < lines.size(); ++i) {
                const QString t = truthKeyOf.value(lines[i].id);
                if (t.isEmpty() || lines[i].endMs - lines[i].startMs < opt.seedMinMs) continue;
                const QVector<float> v = library::fusion::fuse(cache.setFor(lines[i].id, set.modelIds()), set.modelIds());
                if (v.isEmpty()) continue;
                ++idx;
                QString best;
                double bestS = -2.0;
                for (auto it = sum.cbegin(); it != sum.cend(); ++it) {
                    QVector<double> c = it.value();
                    if (it.key() == t) for (int d = 0; d < v.size(); ++d) c[d] -= v[d];
                    double dotv = 0.0, nc = 0.0;
                    for (int d = 0; d < v.size(); ++d) { dotv += c[d] * v[d]; nc += c[d] * c[d]; }
                    const double sc = nc > 0 ? dotv / std::sqrt(nc) : -2.0;
                    if (sc > bestS) { bestS = sc; best = it.key(); }
                }
                ++all;
                ok += best == t;
                if (truthOf[lines[i].id].remote) { ++allRemote; okRemote += best == t; }
            }
            root_["oracleNearestTruthCentroid"] = QJsonObject{{"lines", all}, {"right", ok}, {"remoteLines", allRemote},
                                                              {"remoteRight", okRemote}};
        }
        // Confusion of the remote truth lines: truth key -> (new speaker, basis, side) counts.
        QMap<QString, int> confusion;
        for (int i = 0; i < lines.size() && plan.ran; ++i) {
            const QString t = truthKeyOf.value(lines[i].id);
            if (t.isEmpty() || !truthOf[lines[i].id].remote || seedIds.contains(lines[i].id)) continue;
            const rl::LineDecision& d = plan.result.lines[i];
            ++confusion[QStringLiteral("%1 -> %2 [%3 %4 %5%6]").arg(t, plan.newKey[i], rl::basisName(d.basis),
                                                                  editing::sideName(plan.input[i].side),
                                                                  plan.hasVector[i] ? QStringLiteral("vec") : QStringLiteral("novec"),
                                                                  d.weak ? QStringLiteral(" weak") : QString())];
        }
        QJsonObject conf;
        for (auto it = confusion.cbegin(); it != confusion.cend(); ++it) conf[it.key()] = it.value();
        root_["confusion"] = conf;
        root_["evaluation"] = QJsonObject{{"truthLines", int(truthKeyOf.size())}, {"seedsKept", int(seedIds.size())},
                                          {"baseline", evaluate(before)}, {"new", evaluate(after)},
                                          {"purity", pureN > 0 ? double(pureRight) / pureN : 0.0},
                                          {"purityLines", pureN}};
    }

    int applied = 0;
    if (apply && plan.ran) applied = session.applyRemoteAssignment(plan);
    root_["applied"] = applied;

    if (json) {
        out() << QJsonDocument(root_).toJson(QJsonDocument::Indented);
        out().flush();
        return 0;
    }
    out() << "Sides: local " << sc.local << ", remote " << sc.remote << ", mixed " << sc.mixed << ", unknown " << sc.unknown << "\n";
    out() << "Embedding: " << embedded << " lines in " << embedMs << " ms; assignment " << planMs << " ms\n";
    if (!plan.ran) {
        out() << "Not run: " << plan.skipped << "\n";
        out().flush();
        return 0;
    }
    out() << "Seeds " << plan.result.seeds << ", clusters " << plan.result.clusters.size() << " (dissolved "
          << plan.result.dissolved << ", anonymous " << plan.result.anonymous << ")\n";
    for (const QJsonValue& v : std::as_const(clustersJ)) {
        const QJsonObject o = v.toObject();
        out() << "  #" << o["index"].toInt() << "  " << QString::number(o["lines"].toInt()).rightJustified(4) << " lines "
              << QString::number(o["seedSec"].toDouble(), 'f', 0).rightJustified(5) << " s  -> "
              << o["speaker"].toString().leftJustified(22) << " by " << o["bind"].toString();
        if (o.contains("truthLines"))
            out() << "   truth " << o["truthMajorityLines"].toInt() << "/" << o["truthLines"].toInt() << " "
                  << o["truthMajority"].toString();
        out() << "\n";
    }
    out() << "Lines changing: " << plan.changed << ", uncertain: " << plan.weak << ", new participants: "
          << plan.newParticipants << "\n";
    if (root_.contains("evaluation")) {
        const QJsonObject e = root_["evaluation"].toObject();
        for (const char* phase : {"baseline", "new"}) {
            const QJsonObject t = e[phase].toObject()["text"].toObject();
            out() << phase << ":\n";
            for (auto it = t.constBegin(); it != t.constEnd(); ++it)
                out() << "  " << it.key().leftJustified(32) << " " << it.value().toString() << "\n";
        }
        out() << "Cluster purity: " << QString::number(100.0 * e["purity"].toDouble(), 'f', 1) << "% of "
              << e["purityLines"].toInt() << " truth lines\n";
    }
    if (apply) out() << "Applied: " << applied << " lines changed\n";
    else out() << "Dry run: nothing written (--apply writes the meeting).\n";
    out().flush();
    return 0;
}

} // namespace tanara::cli
