// tanara-cli review. All output is English, no tr() (the Hungarian group labels are omitted).
#include "ReviewCommand.h"
#include "tanara/foundation/time/TimeFormat.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/editing/review/CandidateRanker.h"
#include "tanara/editing/review/ReviewGroups.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/EmbeddingSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cmath>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

constexpr int kTopCandidates = 3;
constexpr int kMaxListedIds = 12;

QString num(double v, int prec = 2) { return std::isnan(v) ? QStringLiteral("-") : QString::number(v, 'f', prec); }
QJsonValue jnum(double v) { return std::isnan(v) ? QJsonValue() : QJsonValue(v); }

QJsonObject evidenceJson(const Evidence& e)
{
    return {{"kind", editing::ranking::evidenceKindName(e.kind)}, {"polarity", editing::ranking::polarityName(e.polarity)},
            {"value", jnum(e.value)}, {"fixTarget", e.fixTarget}};
}

QString evidenceText(const Evidence& e)
{
    const QChar sign = e.polarity == Polarity::Support ? QLatin1Char('+')
                     : e.polarity == Polarity::Contradict ? QLatin1Char('-') : QLatin1Char('~');
    QString s = editing::ranking::evidenceKindName(e.kind) + sign + QLatin1Char(' ') + num(e.value);
    if (!e.fixTarget.isEmpty()) s += QStringLiteral(" [fix: %1]").arg(e.fixTarget);
    return s;
}

QJsonObject candidateJson(const Candidate& c)
{
    QJsonArray ev;
    for (const Evidence& e : c.evidence) ev.append(evidenceJson(e));
    return {{"speakerKey", c.speakerKey}, {"person", c.personName}, {"score", c.score},
            {"otherSide", c.otherSide}, {"linesHere", c.linesHere}, {"evidence", ev}};
}

// The people's stored voiceprints as fusion vectors (including sibling voiceprints, i.e. the
// same sample under other models); only those with the requested dimension.
QHash<QString, QVector<QVector<float>>> fusedPrints(const library::VoiceprintStore& vp, const QStringList& models, int dim)
{
    QHash<QString, QVector<QVector<float>>> out;
    for (const QString& person : vp.people()) {
        for (const QVector<Voiceprint>& group : library::VoiceprintStore::siblingGroups(vp.printsFor(person))) {
            EmbeddingSet set;
            for (const Voiceprint& p : group) set.insert(p.model, p.embedding);
            const QVector<float> f = processing::fusion::fuse(set, models);
            if (!f.isEmpty() && f.size() == dim) out[person].append(f);
        }
    }
    return out;
}

} // namespace

int runReviewCommand(const QStringList& args)
{
    QString folder;
    QStringList models;
    bool json = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == "--json") json = true;
        else if (a == "--models" && i + 1 < args.size()) models = args.at(++i).split(',', Qt::SkipEmptyParts);
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    if (folder.isEmpty()) {
        err() << "Usage: review <meeting-folder> [--json] [--models a,b]\n";
        return 2;
    }
    folder = QFileInfo(folder).absoluteFilePath();

    Meeting m = library::MeetingStore::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    m.folder = folder;
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(folder);
    if (lines.isEmpty()) { err() << "No transcript (transcript.segments.json) in " << folder << "\n"; return 1; }

    editing::ReviewInput in;
    in.meeting = m;
    in.lines = lines;
    in.overlay = editing::speakeredit::loadOverlayFor(folder, lines);
    const app::SettingsManager sm;
    in.userName = sm.settings().userSpeakerName;

    // Embeddings: from the cache (the requested model, else the default, else whichever exists).
    const processing::UtteranceEmbeddingCache cache = processing::UtteranceEmbeddingCache::load(folder, in.overlay.transcriptFingerprint);
    if (models.isEmpty()) {
        const QString rec = processing::VoiceModelRegistry::recommendedModelId();
        const QString def = processing::VoiceModelRegistry::defaultModelId();
        if (cache.models.contains(rec)) models << rec;
        else if (cache.models.contains(def) || cache.models.isEmpty()) models << def;
        else models << cache.models.firstKey();
    }
    models = processing::VoiceModelRegistry::normalizeIds(models);
    int dim = 0;
    for (const TranscriptLine& l : lines) {
        const QVector<float> f = processing::fusion::fuse(cache.setFor(l.id, models), models);
        if (f.isEmpty()) continue;
        in.embeddings.insert(l.id, f);
        dim = int(f.size());
    }
    if (dim > 0) in.personPrints = fusedPrints(library::VoiceprintStore(), models, dim);
    const library::PeopleStore people;
    const QHash<QString, QString> learned = people.defaultSides();
    for (auto it = learned.cbegin(); it != learned.cend(); ++it)
        in.sideHints.learnedSides.insert(it.key().toCaseFolded(), editing::sideFromName(it.value()));

    // Track activity: the cache, otherwise computed in memory (nothing is written to the folder).
    const QString fp = processing::trackactivity::activityFingerprint(m, folder);
    MeetingActivity act = MeetingActivity::load(folder, fp);
    const bool fromCache = !act.isEmpty();
    bool mic = false, loop = false;
    for (const Track& t : m.tracks) {
        mic |= t.active && t.kind == TrackKind::Mic;
        loop |= t.active && t.kind == TrackKind::Loopback;
    }
    if (!fromCache && mic && loop) {
        err() << "Decoding tracks…\n";
        err().flush();
        act = processing::computeMeetingActivity(m, folder);
    }

    const editing::ReviewResult r = analyzeReview(in, act);

    if (json) {
        QJsonObject root;
        root["meetingFolder"] = folder;
        root["models"] = QJsonArray::fromStringList(models);
        root["embeddedLines"] = int(in.embeddings.size());
        root["lines"] = int(lines.size());
        root["sidesActive"] = r.sides.active;
        root["activityFromCache"] = fromCache;
        QJsonArray groups;
        for (const editing::ReviewGroup& g : r.groups) {
            QJsonArray ev;
            for (const Evidence& e : g.evidence) ev.append(evidenceJson(e));
            groups.append(QJsonObject{{"id", g.id}, {"kind", reviewKindName(g.kind)},
                                      {"currentSpeakerKey", g.currentSpeakerKey},
                                      {"proposedSpeakerKey", g.proposedSpeakerKey},
                                      {"proposedPerson", g.proposedPersonName},
                                      {"utteranceIds", QJsonArray::fromStringList(g.utteranceIds)},
                                      {"confirmedBasis", g.confirmedBasis}, {"evidence", ev}});
        }
        root["groups"] = groups;
        QJsonArray ignored;
        for (const editing::LineSide& ls : r.sides.lines)
            if (ls.ignored) ignored.append(ls.utteranceId);
        root["ignoredLines"] = ignored;
        QJsonArray speakers;
        for (const editing::ranking::SpeakerProfile& p : r.context.speakers) {
            if (p.key.isEmpty()) continue;
            QJsonArray self, cands;
            for (const Evidence& e : editing::ranking::selfEvidence(r.context, p.key)) self.append(evidenceJson(e));
            const QVector<Candidate> cs = editing::ranking::rankForSpeaker(r.context, p.key);
            for (int i = 0; i < cs.size() && i < kTopCandidates; ++i) cands.append(candidateJson(cs[i]));
            speakers.append(QJsonObject{{"speakerKey", p.key}, {"person", p.personName}, {"name", p.displayName},
                                        {"side", sideName(p.side)}, {"sideBasis", p.sideBasis},
                                        {"lines", p.lines}, {"confirmedLines", p.confirmedLines},
                                        {"prints", int(p.prints.size())}, {"evidence", self},
                                        {"candidates", cands}});
        }
        root["speakers"] = speakers;
        out() << QJsonDocument(root).toJson(QJsonDocument::Indented);
        out().flush();
        return 0;
    }

    out() << "Lines: " << lines.size() << ", with voice embedding: " << in.embeddings.size()
          << " (models: " << models.join(',') << ")\n";
    out() << "Track sides: " << (r.sides.active ? "active" : "inactive (needs mic + loopback activity)")
          << (r.sides.active && fromCache ? " (activity from cache)" : "") << "\n";

    QStringList ignoredIds;
    for (const editing::LineSide& ls : r.sides.lines)
        if (ls.ignored) ignoredIds << ls.utteranceId;
    out() << "Not identifiable (ignored by voice analysis): " << ignoredIds.size();
    if (!ignoredIds.isEmpty())
        out() << "  " << ignoredIds.mid(0, kMaxListedIds).join(' ')
              << (ignoredIds.size() > kMaxListedIds ? QStringLiteral(" … (+%1)").arg(ignoredIds.size() - kMaxListedIds) : QString());
    out() << "\n";

    out() << "\nReview groups: " << r.groups.size() << "\n";
    for (const editing::ReviewGroup& g : r.groups) {
        const QString target = !g.proposedSpeakerKey.isEmpty() ? g.proposedSpeakerKey
                             : !g.proposedPersonName.isEmpty() ? QStringLiteral("person '%1'").arg(g.proposedPersonName)
                             : g.kind == editing::ReviewKind::ShortLines ? QStringLiteral("-") : QStringLiteral("(new participant)");
        out() << "  " << reviewKindName(g.kind).leftJustified(18) << " " << QString::number(g.utteranceIds.size()).rightJustified(4)
              << " lines  " << (g.currentSpeakerKey.isEmpty() ? QStringLiteral("-") : g.currentSpeakerKey)
              << " -> " << target << "   confirmed basis " << g.confirmedBasis << "\n";
        if (!g.evidence.isEmpty()) {
            QStringList ev;
            for (const Evidence& e : g.evidence) ev << evidenceText(e);
            out() << "      evidence: " << ev.join(QStringLiteral(", ")) << "\n";
        }
        QStringList ids = g.utteranceIds.mid(0, kMaxListedIds);
        out() << "      lines: " << ids.join(' ')
              << (g.utteranceIds.size() > kMaxListedIds ? QStringLiteral(" … (+%1)").arg(g.utteranceIds.size() - kMaxListedIds) : QString())
              << "\n";
    }

    out() << "\nSpeakers (side, lines, confirmed core, prints | top candidates):\n";
    for (const editing::ranking::SpeakerProfile& p : r.context.speakers) {
        if (p.key.isEmpty()) continue;
        out() << "  " << p.displayName.leftJustified(20) << " " << sideName(p.side).leftJustified(7) << " "
              << p.sideBasis.leftJustified(11) << " lines " << QString::number(p.lines).rightJustified(4)
              << "  core " << QString::number(p.confirmedLines).rightJustified(3) << "  prints " << p.prints.size()
              << (p.displayName != p.key ? QStringLiteral("   (%1)").arg(p.key) : QString()) << "\n";
        QStringList self;
        for (const Evidence& e : editing::ranking::selfEvidence(r.context, p.key)) self << evidenceText(e);
        if (!self.isEmpty()) out() << "      why this speaker: " << self.join(QStringLiteral(", ")) << "\n";
        const QVector<Candidate> cs = editing::ranking::rankForSpeaker(r.context, p.key);
        for (int i = 0; i < cs.size() && i < kTopCandidates; ++i) {
            QStringList ev;
            for (const Evidence& e : cs[i].evidence) ev << evidenceText(e);
            out() << "      or: " << (cs[i].speakerKey.isEmpty() ? cs[i].personName : cs[i].speakerKey)
                  << "  score " << num(cs[i].score) << (cs[i].otherSide ? "  (other side)" : "")
                  << "  " << ev.join(QStringLiteral(", ")) << "\n";
        }
    }
    out().flush();
    return 0;
}

namespace {

QString clock(qint64 ms)
{
    return timefmt::clockMs(ms);
}

int setIgnored(const QString& folder, const QVector<TranscriptLine>& lines, const QStringList& ids, bool ignored)
{
    SpeakerOverlay ov = editing::speakeredit::loadOverlayFor(folder, lines);
    QSet<QString> known;
    for (const TranscriptLine& l : lines) known.insert(l.id);
    for (const QString& id : ids)
        if (!known.contains(id)) { err() << "Unknown utterance id: " << id << "\n"; return 1; }
    int changed = 0;
    for (const QString& id : ids) {
        auto it = ov.utterances.find(id);
        if (it == ov.utterances.end()) {
            if (!ignored) continue;
            it = ov.utterances.insert(id, OverlayUtterance());
        }
        if (it->ignored == ignored) continue;
        it->ignored = ignored;
        if (ignored) {          // as in SpeakerEditor::setUtterancesIgnored
            it->rechecked = false;
            it->recheckHint.clear();
        }
        if (it->isDefault()) ov.utterances.erase(it);
        ++changed;
    }
    if (changed > 0 && !editing::speakeredit::saveOverlay(folder, ov)) {
        err() << "Could not write " << editing::speakeredit::overlayPath(folder) << "\n";
        return 1;
    }
    out() << (ignored ? "Marked " : "Unmarked ") << changed << " line(s) as not identifiable"
          << (changed < ids.size() ? QStringLiteral(" (%1 unchanged)").arg(ids.size() - changed) : QString()) << ".\n";
    out().flush();
    return 0;
}

} // namespace

int runLinesCommand(const QStringList& args)
{
    const QString usage = QStringLiteral(
        "Usage: lines <meeting-folder> [--json]\n"
        "       lines ignore <meeting-folder> <utteranceId...>\n"
        "       lines unignore <meeting-folder> <utteranceId...>\n");
    const QString sub = args.value(2);
    const bool edit = sub == QLatin1String("ignore") || sub == QLatin1String("unignore");
    QString folder;
    QStringList ids;
    bool json = false;
    for (int i = edit ? 3 : 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (!edit && a == "--json") json = true;
        else if (a.startsWith("--")) { err() << "Unknown argument: " << a << "\n" << usage; return 2; }
        else if (folder.isEmpty()) folder = a;
        else if (edit) ids << a;
        else { err() << "Unknown argument: " << a << "\n" << usage; return 2; }
    }
    if (folder.isEmpty() || (edit && ids.isEmpty())) { err() << usage; return 2; }
    folder = QFileInfo(folder).absoluteFilePath();

    Meeting m = library::MeetingStore::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(folder);
    if (lines.isEmpty()) { err() << "No transcript (transcript.segments.json) in " << folder << "\n"; return 1; }
    if (edit) return setIgnored(folder, lines, ids, sub == QLatin1String("ignore"));

    const SpeakerOverlay ov = editing::speakeredit::loadOverlayFor(folder, lines);
    // The automatic "talked over" flag (overlap with another resolved speaker), as in the editor.
    QVector<QString> keys(lines.size());
    QVector<editing::speakeredit::TimedLine> timed(lines.size());
    QHash<QString, int> keyIdx;
    for (int i = 0; i < lines.size(); ++i) {
        keys[i] = editing::speakeredit::resolveSpeakerKey(ov, lines[i]);
        auto it = keyIdx.find(keys[i]);
        if (it == keyIdx.end()) it = keyIdx.insert(keys[i], keyIdx.size());
        timed[i] = {lines[i].startMs, lines[i].endMs, it.value()};
    }
    const QVector<bool> autoNoisy = editing::speakeredit::computeOverlapNoisy(timed);

    QJsonArray arr;
    int ignoredCount = 0;
    QStringList rows;
    for (int i = 0; i < lines.size(); ++i) {
        const TranscriptLine& l = lines[i];
        const auto o = ov.utterances.constFind(l.id);
        const bool has = o != ov.utterances.constEnd();
        const bool corrected = has && o->corrected;
        const bool confirmed = has && o->confirmed;
        const bool noisy = (has && o->noisy.has_value()) ? *o->noisy : autoNoisy.value(i);
        const bool ignored = has && o->ignored;
        const bool rechecked = has && o->rechecked && !corrected && !confirmed && !ignored;
        ignoredCount += ignored ? 1 : 0;
        const QString name = editing::speakeredit::speakerDisplayName(ov, m.speakerMap, keys[i]);
        if (json) {
            arr.append(QJsonObject{{"id", l.id}, {"startMs", double(l.startMs)}, {"endMs", double(l.endMs)},
                                   {"speakerKey", keys[i]}, {"speaker", name}, {"rawLabel", l.rawLabel},
                                   {"corrected", corrected}, {"confirmed", confirmed}, {"noisy", noisy},
                                   {"ignored", ignored}, {"rechecked", rechecked}});
            continue;
        }
        QStringList flags;
        if (corrected) flags << "corrected";
        if (confirmed) flags << "confirmed";
        if (noisy) flags << "not-a-sample";
        if (ignored) flags << "ignored";
        if (rechecked) flags << "rechecked";
        rows << QStringLiteral("  %1  %2  %3  %4  %5").arg(clock(l.startMs), l.id.leftJustified(14),
                                                         QString::number((l.endMs - l.startMs) / 1000.0, 'f', 1).rightJustified(6),
                                                         name.leftJustified(20), flags.join(' '));
    }
    if (json) {
        out() << QJsonDocument(QJsonObject{{"meetingFolder", folder}, {"lines", arr},
                                           {"ignored", ignoredCount}}).toJson(QJsonDocument::Indented);
        out().flush();
        return 0;
    }
    out() << "Lines: " << lines.size() << ", not identifiable (ignored): " << ignoredCount << "\n";
    out() << "  start     id              dur(s)  speaker               flags\n";
    for (const QString& r : std::as_const(rows)) out() << r << "\n";
    out().flush();
    return 0;
}

} // namespace tanara::cli
