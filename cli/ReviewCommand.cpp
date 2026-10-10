// tanara-cli review. Minden kimenet angol, tr() nélkül (a csoportok magyar feliratai kimaradnak).
#include "ReviewCommand.h"

#include "tanara/SettingsManager.h"
#include "tanara/edit/CandidateRanker.h"
#include "tanara/edit/ReviewGroups.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/EmbeddingSet.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

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
    return {{"kind", ranking::evidenceKindName(e.kind)}, {"polarity", ranking::polarityName(e.polarity)},
            {"value", jnum(e.value)}, {"fixTarget", e.fixTarget}};
}

QString evidenceText(const Evidence& e)
{
    const QChar sign = e.polarity == Polarity::Support ? QLatin1Char('+')
                     : e.polarity == Polarity::Contradict ? QLatin1Char('-') : QLatin1Char('~');
    QString s = ranking::evidenceKindName(e.kind) + sign + QLatin1Char(' ') + num(e.value);
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

// A személyek tárolt lenyomatai fúziós vektorként (a testvér-lenyomatok — ugyanaz a minta más
// modellekkel — együtt); csak a megadott dimenziójúak.
QHash<QString, QVector<QVector<float>>> fusedPrints(const VoiceprintStore& vp, const QStringList& models, int dim)
{
    QHash<QString, QVector<QVector<float>>> out;
    for (const QString& person : vp.people()) {
        for (const QVector<Voiceprint>& group : VoiceprintStore::siblingGroups(vp.printsFor(person))) {
            EmbeddingSet set;
            for (const Voiceprint& p : group) set.insert(p.model, p.embedding);
            const QVector<float> f = fusion::fuse(set, models);
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

    Meeting m = MeetingStore::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    m.folder = folder;
    const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(folder);
    if (lines.isEmpty()) { err() << "No transcript (transcript.segments.json) in " << folder << "\n"; return 1; }

    ReviewInput in;
    in.meeting = m;
    in.lines = lines;
    in.overlay = speakeredit::loadOverlayFor(folder, lines);
    const SettingsManager sm;
    in.userName = sm.settings().userSpeakerName;

    // Beágyazások: a cache-ből (a kért, vagy az alapmodell, vagy amelyik van).
    const UtteranceEmbeddingCache cache = UtteranceEmbeddingCache::load(folder, in.overlay.transcriptFingerprint);
    if (models.isEmpty()) {
        const QString def = VoiceModelRegistry::defaultModelId();
        if (cache.models.contains(def) || cache.models.isEmpty()) models << def;
        else models << cache.models.firstKey();
    }
    models = VoiceModelRegistry::normalizeIds(models);
    int dim = 0;
    for (const TranscriptLine& l : lines) {
        const QVector<float> f = fusion::fuse(cache.setFor(l.id, models), models);
        if (f.isEmpty()) continue;
        in.embeddings.insert(l.id, f);
        dim = int(f.size());
    }
    if (dim > 0) in.personPrints = fusedPrints(VoiceprintStore(), models, dim);
    const PeopleStore people;
    const QHash<QString, QString> learned = people.defaultSides();
    for (auto it = learned.cbegin(); it != learned.cend(); ++it)
        in.sideHints.learnedSides.insert(it.key().toCaseFolded(), sideFromName(it.value()));

    // Sáv-aktivitás: a cache, különben a memóriában számolva (a mappába nem ír).
    const QString fp = trackactivity::activityFingerprint(m, folder);
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
        act = computeMeetingActivity(m, folder);
    }

    const ReviewResult r = analyzeReview(in, act);

    if (json) {
        QJsonObject root;
        root["meetingFolder"] = folder;
        root["models"] = QJsonArray::fromStringList(models);
        root["embeddedLines"] = int(in.embeddings.size());
        root["lines"] = int(lines.size());
        root["sidesActive"] = r.sides.active;
        root["activityFromCache"] = fromCache;
        QJsonArray groups;
        for (const ReviewGroup& g : r.groups) {
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
        QJsonArray speakers;
        for (const ranking::SpeakerProfile& p : r.context.speakers) {
            if (p.key.isEmpty()) continue;
            QJsonArray self, cands;
            for (const Evidence& e : ranking::selfEvidence(r.context, p.key)) self.append(evidenceJson(e));
            const QVector<Candidate> cs = ranking::rankForSpeaker(r.context, p.key);
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

    out() << "\nReview groups: " << r.groups.size() << "\n";
    for (const ReviewGroup& g : r.groups) {
        const QString target = !g.proposedSpeakerKey.isEmpty() ? g.proposedSpeakerKey
                             : !g.proposedPersonName.isEmpty() ? QStringLiteral("person '%1'").arg(g.proposedPersonName)
                             : g.kind == ReviewKind::ShortLines ? QStringLiteral("-") : QStringLiteral("(new participant)");
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
    for (const ranking::SpeakerProfile& p : r.context.speakers) {
        if (p.key.isEmpty()) continue;
        out() << "  " << p.displayName.leftJustified(20) << " " << sideName(p.side).leftJustified(7) << " "
              << p.sideBasis.leftJustified(11) << " lines " << QString::number(p.lines).rightJustified(4)
              << "  core " << QString::number(p.confirmedLines).rightJustified(3) << "  prints " << p.prints.size()
              << (p.displayName != p.key ? QStringLiteral("   (%1)").arg(p.key) : QString()) << "\n";
        QStringList self;
        for (const Evidence& e : ranking::selfEvidence(r.context, p.key)) self << evidenceText(e);
        if (!self.isEmpty()) out() << "      why this speaker: " << self.join(QStringLiteral(", ")) << "\n";
        const QVector<Candidate> cs = ranking::rankForSpeaker(r.context, p.key);
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

} // namespace tanara::cli
