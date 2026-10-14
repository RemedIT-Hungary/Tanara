// tanara-cli voice-models / voice-eval. All output is English, no tr().
#include "VoiceCommands.h"

#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceEval.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QTextStream>

#include <cmath>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString appDir()
{
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
}

QString modelsDirOf(const app::SettingsManager& sm)
{
    return sm.paths().modelsDir();
}

QString pad(const QString& s, int w) { return s.leftJustified(w, QLatin1Char(' ')); }

int listModels(const app::SettingsManager& sm)
{
    const QString meta = modelsDirOf(sm);
    const QStringList enabled = sm.enabledVoiceModels();
    const QStringList head{"ID", "NAME", "ENABLED", "FILE", "DIM", "LICENSE", "SOURCE"};
    QVector<QStringList> rows;
    for (const processing::VoiceModelSpec& s : processing::VoiceModelRegistry::builtin()) {
        const QString path = processing::VoiceModelRegistry::resolvePath(s, meta, appDir());
        rows.append({s.id,
                     s.id == processing::VoiceModelRegistry::recommendedModelId() ? s.displayName + QStringLiteral(" (recommended)")
                                                                       : s.displayName,
                     enabled.contains(s.id) ? "yes" : "no",
                     QFileInfo::exists(path) ? QDir::toNativeSeparators(path) : QStringLiteral("-"),
                     s.dim > 0 ? QString::number(s.dim) : QStringLiteral("?"), s.license, s.sourceUrl});
    }
    QVector<int> width(head.size());
    for (int c = 0; c < head.size(); ++c) {
        width[c] = head[c].size();
        for (const QStringList& r : rows) width[c] = std::max(width[c], int(r[c].size()));
    }
    auto print = [&](const QStringList& r) {
        QStringList cells;
        for (int c = 0; c < r.size(); ++c) cells << (c + 1 < r.size() ? pad(r[c], width[c]) : r[c]);
        out() << cells.join(QStringLiteral("  ")) << "\n";
    };
    print(head);
    for (const QStringList& r : rows) print(r);
    for (const QString& id : enabled)
        if (!processing::VoiceModelRegistry::spec(id))
            out() << "warning: unknown model id enabled in settings: " << id << "\n";
    out().flush();
    return 0;
}

int setEnabled(app::SettingsManager& sm, const QString& id, bool on)
{
    if (!processing::VoiceModelRegistry::spec(id)) {
        err() << "Unknown voice model: " << id << " (see: tanara-cli voice-models)\n";
        err().flush();
        return 1;
    }
    QStringList ids = sm.enabledVoiceModels();
    if (on && !ids.contains(id)) ids << id;
    if (!on) ids.removeAll(id);
    sm.setEnabledVoiceModels(ids);
    out() << (on ? "Enabled: " : "Disabled: ") << id << "\n"
          << "Enabled models: " << (sm.enabledVoiceModels().isEmpty() ? QStringLiteral("(none)")
                                                                     : sm.enabledVoiceModels().join(", "))
          << "\n";
    const QString path = processing::VoiceModelRegistry::resolvePath(*processing::VoiceModelRegistry::spec(id), modelsDirOf(sm), appDir());
    if (on && !QFileInfo::exists(path))
        out() << "Note: the model file is missing (" << QDir::toNativeSeparators(path)
              << "); fetch it with: tanara-cli voice-models fetch " << id << "\n";
    out().flush();
    return 0;
}

int fetchModel(const app::SettingsManager& sm, const QString& id, bool force)
{
    const auto spec = processing::VoiceModelRegistry::spec(id);
    if (!spec) {
        err() << "Unknown voice model: " << id << "\n";
        return 1;
    }
    if (spec->downloadUrl.isEmpty()) {
        err() << "No download URL for " << id << "\n";
        return 1;
    }
    const QString target = processing::VoiceModelDownloader::targetPath(*spec, modelsDirOf(sm));
    out() << "Model:    " << spec->displayName << " (" << spec->id << ")\n"
          << "License:  " << spec->license << "\n"
          << "Source:   " << spec->sourceUrl << "\n"
          << "Download: " << spec->downloadUrl << "\n"
          << "Target:   " << QDir::toNativeSeparators(target) << "\n";
    out().flush();

    // The download goes through the VoiceModelDownloader shared with the GUI (QSaveFile, size check).
    processing::VoiceModelDownloader dl;
    QEventLoop loop;
    bool done = false, ok = false;
    QString error;
    int lastPct = -1;
    QObject::connect(&dl, &processing::VoiceModelDownloader::progress, &loop, [&](const QString&, qint64 got, qint64 total) {
        const int pct = total > 0 ? int(got * 100 / total) : -1;
        if (pct == lastPct) return;
        lastPct = pct;
        err() << "\rDownloading: " << (got / 1024 / 1024) << " MiB"
              << (pct >= 0 ? QStringLiteral(" (%1%)").arg(pct) : QString());
        err().flush();
    });
    QObject::connect(&dl, &processing::VoiceModelDownloader::finished, &loop, [&](const QString&, bool success, const QString& e) {
        done = true;
        ok = success;
        error = e;
        loop.quit();
    });
    if (dl.start(*spec, target, force) && !done) loop.exec();
    if (lastPct >= 0) err() << "\n";
    if (!ok) {
        if (dl.lastFailure(id) == processing::VoiceModelDownloader::Failure::Exists)
            err() << "The file already exists; use --force to overwrite it.\n";
        else
            err() << "Download failed: " << error << "\n";
        err().flush();
        return 1;
    }
    out() << "Saved " << QFileInfo(target).size() << " bytes to " << QDir::toNativeSeparators(target) << "\n"
          << "Enable it with: tanara-cli voice-models enable " << id << "\n";
    out().flush();
    return 0;
}

// voice-models verify <id>: checks the model file (size match). Exit 0: OK (or nothing to
// compare against), 1: missing / mismatch / unknown id.
int verifyModel(const app::SettingsManager& sm, const QString& id)
{
    const auto spec = processing::VoiceModelRegistry::spec(id);
    if (!spec) { err() << "Unknown voice model: " << id << "\n"; return 1; }
    const QString path = processing::VoiceModelRegistry::resolvePath(*spec, modelsDirOf(sm), appDir());
    QString detail;
    const processing::VoiceModelDownloader::VerifyResult r = processing::VoiceModelDownloader::verifyFile(*spec, path, &detail);
    out() << "File:   " << QDir::toNativeSeparators(path) << "\n";
    switch (r) {
    case processing::VoiceModelDownloader::VerifyResult::Ok:
        out() << "Result: OK (" << spec->sizeBytes << " bytes)\n";
        break;
    case processing::VoiceModelDownloader::VerifyResult::Unverifiable:
        out() << "Result: present, not verifiable (" << detail << ")\n";
        break;
    case processing::VoiceModelDownloader::VerifyResult::Missing:
        out() << "Result: MISSING\n";
        break;
    case processing::VoiceModelDownloader::VerifyResult::SizeMismatch:
        out() << "Result: SIZE MISMATCH (" << detail << ")\n"
              << "Fetch it again with: tanara-cli voice-models fetch " << id << " --force\n";
        break;
    }
    out().flush();
    return (r == processing::VoiceModelDownloader::VerifyResult::Ok || r == processing::VoiceModelDownloader::VerifyResult::Unverifiable) ? 0 : 1;
}

// voice-models delete <id>: deletes the DOWNLOADED file (<metaDir>/models), not a bundled one.
int deleteModel(const app::SettingsManager& sm, const QString& id)
{
    const auto spec = processing::VoiceModelRegistry::spec(id);
    if (!spec) { err() << "Unknown voice model: " << id << "\n"; return 1; }
    QString error;
    if (!processing::VoiceModelDownloader::deleteModelFile(*spec, modelsDirOf(sm), &error)) {
        err() << error << "\n";
        err().flush();
        return 1;
    }
    out() << "Deleted " << QDir::toNativeSeparators(processing::VoiceModelDownloader::targetPath(*spec, modelsDirOf(sm))) << "\n";
    const QString still = processing::VoiceModelRegistry::resolvePath(*spec, modelsDirOf(sm), appDir());
    if (QFileInfo::exists(still))
        out() << "Note: a bundled copy is still used: " << QDir::toNativeSeparators(still) << "\n";
    out().flush();
    return 0;
}

QString fmt(double v) { return std::isnan(v) ? QStringLiteral("   -  ") : QString::number(v, 'f', 3).rightJustified(6); }

void printReport(const processing::VoiceEvalReport& r)
{
    out() << "Models: " << r.models.join(", ") << "   (lines >= " << r.minMs << " ms are embedded)\n";
    out() << "Speakers:\n";
    for (int i = 0; i < r.speakers.size(); ++i)
        out() << "  [" << i << "] " << r.speakers[i].name << "  (" << r.speakers[i].key << ")\n";
    for (const processing::VoiceEvalSpace& sp : r.spaces) {
        out() << "\n== " << sp.id << " ==\n";
        out() << "  (1) coverage: " << sp.linesCovered << " / " << sp.linesEligible << " eligible lines ("
              << sp.linesTotal << " total)\n";
        out() << "  (2) core cosine matrix (cores from confirmed/corrected lines; - = no core):\n";
        out() << "        ";
        for (int j = 0; j < r.speakers.size(); ++j) out() << QStringLiteral("   [%1]").arg(j, 2);
        out() << "   core lines\n";
        for (int i = 0; i < r.speakers.size(); ++i) {
            out() << QStringLiteral("    [%1]").arg(i, 2);
            for (int j = 0; j < r.speakers.size(); ++j) out() << " " << fmt(sp.coreCosine[i][j]);
            out() << "   " << sp.coreLines.value(i) << "\n";
        }
        out() << "  (3) 2-means split per speaker (sub-centroid cosine; lines A/B):\n";
        for (int i = 0; i < r.speakers.size(); ++i) {
            const processing::VoiceEvalSplit& s = sp.splits.value(i);
            out() << QStringLiteral("    [%1] ").arg(i, 2)
                  << (std::isnan(s.centroidCosine) ? QStringLiteral("  -   (%1 lines)").arg(s.lines)
                                                   : QStringLiteral("%1   %2/%3").arg(fmt(s.centroidCosine))
                                                         .arg(s.sizeA).arg(s.sizeB))
                  << "\n";
        }
        out() << "  (4) lines closer to another core by >= " << processing::kVoiceEvalMargin << ": " << sp.closerToOther
              << " / " << sp.closerChecked << "\n";
    }
    out().flush();
}

} // namespace

int runVoiceModelsCommand(const QStringList& args)
{
    app::SettingsManager sm;
    const QString sub = args.value(2);
    if (sub.isEmpty() || sub == "list") return listModels(sm);
    if (sub == "enable" || sub == "disable") {
        const QString id = args.value(3);
        if (id.isEmpty()) { err() << "Usage: voice-models " << sub << " <id>\n"; return 2; }
        return setEnabled(sm, id, sub == "enable");
    }
    if (sub == "fetch") {
        const QString id = args.value(3);
        if (id.isEmpty()) { err() << "Usage: voice-models fetch <id> [--force]\n"; return 2; }
        return fetchModel(sm, id, args.contains(QStringLiteral("--force")));
    }
    if (sub == "verify" || sub == "delete") {
        const QString id = args.value(3);
        if (id.isEmpty()) { err() << "Usage: voice-models " << sub << " <id>\n"; return 2; }
        return sub == "verify" ? verifyModel(sm, id) : deleteModel(sm, id);
    }
    err() << "Usage: voice-models [list | enable <id> | disable <id> | fetch <id> [--force] | verify <id> | delete <id>]\n";
    return 2;
}

int runVoiceEvalCommand(const QStringList& args)
{
    QString folder;
    QStringList models;
    qint64 minMs = editing::speakeredit::kMinEmbedMs;
    bool json = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == "--models" && i + 1 < args.size()) models = args.at(++i).split(',', Qt::SkipEmptyParts);
        else if (a == "--min-ms" && i + 1 < args.size()) minMs = args.at(++i).toLongLong();
        else if (a == "--json") json = true;
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    if (folder.isEmpty()) {
        err() << "Usage: voice-eval <meeting-folder> [--models a,b] [--min-ms 1500] [--json]\n";
        return 2;
    }
    folder = QFileInfo(folder).absoluteFilePath();

    app::SettingsManager sm;
    const QString meta = modelsDirOf(sm);
    if (models.isEmpty()) models = sm.enabledVoiceModels();
    models = processing::VoiceModelRegistry::normalizeIds(models);
    QVector<processing::VoiceModelEntry> entries;
    for (const QString& id : models) {
        const auto spec = processing::VoiceModelRegistry::spec(id);
        if (!spec) { err() << "Unknown voice model: " << id << "\n"; return 1; }
        const QString path = processing::VoiceModelRegistry::resolvePath(*spec, meta, appDir());
        if (!QFileInfo::exists(path)) {
            err() << "Model file missing for " << id << ": " << QDir::toNativeSeparators(path) << "\n";
            return 1;
        }
        entries.append({*spec, path});
    }
    if (entries.isEmpty()) { err() << "No voice model selected.\n"; return 1; }
    const processing::VoiceEmbedderSet set(entries);

    // READ-only: meeting.json, transcript lines, overlay; the cache is neither loaded nor written.
    Meeting m = library::MeetingRepository::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    m.folder = folder;
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(folder);
    if (lines.isEmpty()) { err() << "No transcript (transcript.segments.json) in " << folder << "\n"; return 1; }
    const SpeakerOverlay ov = editing::speakeredit::loadOverlayFor(folder, lines);
    const QString audio = editing::speakeredit::mixdownPath(m);
    if (!QFileInfo::exists(audio)) { err() << "Mixdown audio missing: " << audio << "\n"; return 1; }

    // Speakers (in order of appearance) and the noisy lines, using the editor's rules.
    QVector<processing::VoiceEvalSpeaker> speakers;
    QHash<QString, int> speakerIdx;
    QVector<int> lineSpeaker;
    QVector<editing::speakeredit::TimedLine> timed;
    for (const TranscriptLine& l : lines) {
        const QString key = editing::speakeredit::resolveSpeakerKey(ov, l);
        auto it = speakerIdx.constFind(key);
        if (it == speakerIdx.constEnd()) {
            it = speakerIdx.insert(key, int(speakers.size()));
            speakers.append({key, editing::speakeredit::speakerDisplayName(ov, m.speakerMap, key)});
        }
        lineSpeaker.append(*it);
        timed.append({l.startMs, l.endMs, *it});
    }
    const QVector<bool> autoNoisy = editing::speakeredit::computeOverlapNoisy(timed);

    auto embedder = voiceUtteranceEmbedderFactory(set, media::FfmpegMediaTool::fromSettings(sm.settings().media))();
    if (!embedder || !embedder->open(audio)) {
        err() << "Cannot open audio / models: " << (embedder ? embedder->lastError() : QString()) << "\n";
        return 1;
    }
    QVector<processing::VoiceEvalLine> evalLines;
    int done = 0, todo = 0;
    for (const TranscriptLine& l : lines) if (l.endMs - l.startMs >= minMs) ++todo;
    for (int i = 0; i < lines.size(); ++i) {
        const TranscriptLine& l = lines[i];
        processing::VoiceEvalLine el;
        el.id = l.id;
        el.durationMs = l.endMs - l.startMs;
        el.speaker = lineSpeaker[i];
        const auto o = ov.utterances.constFind(l.id);
        el.locked = o != ov.utterances.constEnd() && (o->corrected || o->confirmed);
        el.noisy = (o != ov.utterances.constEnd() && o->noisy.has_value()) ? *o->noisy : autoNoisy.value(i);
        if (el.durationMs >= minMs) {
            // The same window as in the editor: at most kMaxEmbedMs from the middle of the line.
            qint64 s = l.startMs, e = l.endMs;
            if (e - s > editing::speakeredit::kMaxEmbedMs) {
                s = (l.startMs + l.endMs) / 2 - editing::speakeredit::kMaxEmbedMs / 2;
                e = s + editing::speakeredit::kMaxEmbedMs;
            }
            const EmbeddingSet got = embedder->embedAll(s, e, set.modelIds());
            for (auto it = got.cbegin(); it != got.cend(); ++it)
                if (!it.value().isEmpty()) el.vectors.insert(it.key(), it.value());
            ++done;
            err() << "\rEmbedding lines: " << done << " / " << todo;   // progress: stderr
            err().flush();
        }
        evalLines.append(el);
    }
    err() << "\n";
    err().flush();

    const processing::VoiceEvalReport report = evaluateVoices(evalLines, speakers, set.modelIds(), minMs);
    if (json) {
        QJsonObject root = voiceEvalToJson(report);
        root[QStringLiteral("meetingFolder")] = folder;
        out() << QJsonDocument(root).toJson(QJsonDocument::Indented);
        out().flush();
    } else {
        printReport(report);
    }
    return 0;
}

} // namespace tanara::cli
