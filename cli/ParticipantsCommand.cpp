// tanara-cli participants. Minden kimenet angol, tr() nélkül.
#include "ParticipantsCommand.h"

#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/ParticipantAnalysis.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceEmbedderSet.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

// A bizonyíték-szövegek a GUI-ba szánt magyar rövid feliratok; a CLI a gépi nevüket írja.
QString evidenceText(const Evidence& e)
{
    QString s = evidenceKindName(e.kind) + QLatin1Char(':') + polarityName(e.polarity);
    if (e.kind == EvidenceKind::Voice && e.value >= 0.0) s += QStringLiteral("(%1)").arg(e.value, 0, 'f', 2);
    if (!e.fixTarget.isEmpty()) s += QStringLiteral("[fix:%1]").arg(e.fixTarget);
    return s;
}

QString groupLabel(ParticipantGroup g)
{
    switch (g) {
    case ParticipantGroup::Sure:            return QStringLiteral("SURE");
    case ParticipantGroup::Doubt:           return QStringLiteral("DOUBT");
    case ParticipantGroup::InvitedNotHeard: return QStringLiteral("INVITED, NOT HEARD");
    }
    return QStringLiteral("DOUBT");
}

QJsonObject participantJson(const Participant& p)
{
    QJsonObject o = toJson(p);
    o[QStringLiteral("group")] = participantGroupName(participants::participantGroup(p));
    o[QStringLiteral("defaultChecked")] = participants::defaultChecked(p);
    return o;
}

void printText(const Meeting& m, const QString& analysisNote)
{
    out() << "Tracks:\n";
    for (const Track& t : m.tracks)
        out() << "  " << t.id.leftJustified(24) << " speech "
              << (t.speechRatio < 0 ? QStringLiteral("  -  ") : QStringLiteral("%1%").arg(qRound(t.speechRatio * 100)).rightJustified(5))
              << "  " << (t.included() ? QStringLiteral("in mixdown") : QStringLiteral("excluded (%1)").arg(t.excludedReason))
              << "\n";
    if (!analysisNote.isEmpty()) out() << "\n" << analysisNote << "\n";
    for (ParticipantGroup g : {ParticipantGroup::Sure, ParticipantGroup::Doubt, ParticipantGroup::InvitedNotHeard}) {
        QVector<const Participant*> list;
        for (const Participant& p : m.participants)
            if (participants::participantGroup(p) == g) list << &p;
        if (list.isEmpty()) continue;
        out() << "\n" << groupLabel(g) << " (" << list.size() << ")\n";
        for (const Participant* p : list) {
            QStringList ev, raw;
            for (const Evidence& e : p->evidence) ev << evidenceText(e);
            for (const QString& r : p->rawSpeakerIds) raw << participants::rawDisplay(r);
            out() << "  [" << (participants::defaultChecked(*p) ? 'x' : ' ') << "] "
                  << (p->personName.isEmpty() ? QStringLiteral("(unknown voice)") : p->personName).leftJustified(22)
                  << " " << p->id.leftJustified(11) << " " << participantSourceName(p->source).leftJustified(8)
                  << " sides " << (p->sides.isEmpty() ? QStringLiteral("-") : p->sides.join(QLatin1Char('+'))).leftJustified(13)
                  << " talk " << QString::number(qRound(p->talkShare * 100)).rightJustified(3) << "%"
                  << (p->approved ? "  approved" : "") << "\n";
            out() << "      evidence: " << (ev.isEmpty() ? QStringLiteral("-") : ev.join(QStringLiteral(", "))) << "\n";
            if (!raw.isEmpty()) out() << "      raw speakers: " << raw.join(QStringLiteral(", ")) << "\n";
        }
    }
    if (m.participants.isEmpty()) out() << "\nNo participants yet (run with --analyze).\n";
    if (m.approval)
        out() << "\nDecision at " << m.approval->at << ": "
              << (m.approval->solo ? "only me" : m.approval->skipped ? "skipped" : "approved") << "\n";
    out().flush();
}

} // namespace

int runParticipantsCommand(const QStringList& args)
{
    QString folder;
    bool json = false, analyze = false, write = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == "--json") json = true;
        else if (a == "--analyze") analyze = true;
        else if (a == "--write") write = true;
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    if (folder.isEmpty() || (write && !analyze)) {
        err() << "Usage: participants <meeting-folder> [--analyze [--write]] [--json]\n";
        return 2;
    }
    folder = QFileInfo(folder).absoluteFilePath();
    Meeting m = MeetingStore::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    m.folder = folder;

    QString note;
    ParticipantAnalysisFile file = ParticipantAnalysisFile::load(folder);
    if (analyze) {
        const SettingsManager sm;
        const QString meta = paths::resolveMetadataDir(sm.settings().metadataDir);
        const QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
        const VoiceEmbedderSet set = VoiceEmbedderSet::fromSettings(sm.enabledVoiceModels(), meta, appDir);
        if (set.isEmpty() || !set.ensureLoaded()) {
            err() << "No usable voice model (see: tanara-cli voice-models).\n";
            return 1;
        }
        const QString fp = trackactivity::activityFingerprint(m, folder);
        MeetingActivity act = MeetingActivity::load(folder, fp);
        const bool fromCache = !act.isEmpty();
        if (!fromCache) {
            err() << "Decoding tracks…\n";
            err().flush();
            act = computeMeetingActivity(m, folder);
        }
        err() << "Embedding speech windows…\n";
        err().flush();
        ClusterSet cs = participants::computeVoiceClusters(m, act, set, participants::filePcmReader(m));
        if (!cs.error.isEmpty()) { err() << "Analysis failed: " << cs.error << "\n"; return 1; }

        const VoiceprintStore prints(QDir(meta).filePath(QStringLiteral("voiceprints.json")));
        participants::CandidateContext ctx;
        ctx.selfName = sm.settings().userSpeakerName.trimmed();
        ctx.rank = [&prints](const EmbeddingSet& q) { return prints.rankedMatches(q, q.keys()); };
        ctx.hasVoiceprint = [&prints](const QString& n) { return prints.printCount(n) > 0; };
        m.participants = participants::buildParticipants(cs, m, ctx);
        file.at = QDateTime::currentDateTime().toString(Qt::ISODate);
        file.modelIds = cs.modelIds;
        file.tracksKey = fp;
        file.clusters = cs.clusters;
        note = QStringLiteral("Analysis: %1 windows, %2 clusters, models %3")
                   .arg(cs.windows).arg(cs.clusters.size()).arg(cs.modelIds.join(QLatin1Char(',')));
        const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(folder);
        if (!lines.isEmpty()) participants::bindRawSpeakers(m.participants, file.clusters, lines);

        if (write) {
            if (!fromCache && !act.save(folder))
                err() << "warning: cannot write " << MeetingActivity::filePath(folder) << "\n";
            if (!file.save(folder)) { err() << "Cannot write " << ParticipantAnalysisFile::filePath(folder) << "\n"; return 1; }
            QSaveFile f(QDir(folder).filePath(QStringLiteral("meeting.json")));
            if (!f.open(QIODevice::WriteOnly)
                || f.write(QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
                err() << "Cannot write meeting.json in " << folder << "\n";
                return 1;
            }
            note += QStringLiteral(" (written)");
        }
    }

    if (json) {
        QJsonObject root;
        root["meetingFolder"] = folder;
        QJsonArray tracks;
        for (const Track& t : m.tracks)
            tracks.append(QJsonObject{{"id", t.id}, {"speechRatio", t.speechRatio},
                                      {"included", t.included()}, {"excludedReason", t.excludedReason}});
        root["tracks"] = tracks;
        QJsonArray ps;
        for (const Participant& p : m.participants) ps.append(participantJson(p));
        root["participants"] = ps;
        if (m.approval) root["approval"] = toJson(*m.approval);
        if (!file.isEmpty())
            root["analysis"] = QJsonObject{{"at", file.at}, {"modelIds", QJsonArray::fromStringList(file.modelIds)},
                                           {"clusters", int(file.clusters.size())}};
        out() << QJsonDocument(root).toJson(QJsonDocument::Indented);
        out().flush();
    } else {
        printText(m, note);
    }
    return 0;
}

} // namespace tanara::cli
