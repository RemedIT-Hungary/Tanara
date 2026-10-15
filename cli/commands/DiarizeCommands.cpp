// tanara-cli diarize-probe. All output is English, no tr().
#include "DiarizeCommands.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/diarization/LocalDiarizer.h"
#include "tanara/processing/diarization/PyannoteSegmenter.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cstring>

namespace tanara::cli {

namespace {

QTextStream& sout() { static QTextStream s(stdout); return s; }
QTextStream& serr() { static QTextStream s(stderr); return s; }

const char* kUsage =
    "Usage: diarize-probe <audio> [--model <id>] [--shift <ratio>] [--min-on <s>] [--min-off <s>]\n"
    "                     [--threads <n>] [--batch <n>] [--json] [--windows]\n"
    "                     [--cluster [--threshold <d>] [--num-speakers <n>] [--voice-models a,b]]\n"
    "  --cluster: embed + cluster the segments (the local diarization, D4) with the enabled voice models\n"
    "             (or --voice-models) and print the clustered turns; the default shift is then 0.25.\n";

QString hms(double sec)
{
    const int ms = int(sec * 1000.0 + 0.5);
    return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

int runDiarizeProbeCommand(const QStringList& args)
{
    using namespace processing;
    QString audio, modelId = VoiceModelRegistry::defaultSegmentationModelId();
    SegmenterOptions opts;
    OnnxSegmentationOptions ort;
    bool json = false, windows = false, cluster = false, shiftGiven = false;
    ClusteringOptions copts;
    QStringList voiceModels;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&](bool* ok) { *ok = i + 1 < args.size(); return *ok ? args[++i] : QString(); };
        bool ok = true;
        if (a == "--model") modelId = next(&ok);
        else if (a == "--shift") { opts.windowShiftRatio = next(&ok).toDouble(&ok); shiftGiven = true; }
        else if (a == "--cluster") cluster = true;
        else if (a == "--threshold") copts.threshold = next(&ok).toDouble(&ok);
        else if (a == "--num-speakers") copts.numClusters = next(&ok).toInt(&ok);
        else if (a == "--voice-models") voiceModels = next(&ok).split(QLatin1Char(','), Qt::SkipEmptyParts);
        else if (a == "--min-on") opts.minDurationOn = next(&ok).toDouble(&ok);
        else if (a == "--min-off") opts.minDurationOff = next(&ok).toDouble(&ok);
        else if (a == "--threads") ort.intraOpThreads = next(&ok).toInt(&ok);
        else if (a == "--batch") opts.batchSize = next(&ok).toInt(&ok);
        else if (a == "--json") json = true;
        else if (a == "--windows") windows = true;
        else if (a.startsWith("--") || !audio.isEmpty()) ok = false;
        else audio = a;
        if (!ok) { serr() << kUsage; return 2; }
    }
    if (cluster && !shiftGiven) opts.windowShiftRatio = LocalDiarizer::kDefaultWindowShift;
    if (audio.isEmpty() || opts.windowShiftRatio <= 0 || opts.windowShiftRatio > 1) { serr() << kUsage; return 2; }
    if (!QFileInfo::exists(audio)) { serr() << "No such file: " << audio << "\n"; return 1; }
    const std::optional<VoiceModelSpec> spec = VoiceModelRegistry::spec(modelId);
    if (!spec || spec->kind != VoiceModelKind::Segmentation) {
        serr() << "Unknown segmentation model: " << modelId << " (see: tanara-cli voice-models)\n";
        return 1;
    }

    app::SettingsManager sm;
    const QString appDir = QCoreApplication::applicationDirPath();
    QString error;
    QElapsedTimer clock;
    clock.start();
    std::unique_ptr<PyannoteSegmenter> seg = PyannoteSegmenter::open(*spec, sm.paths().modelsDir(), appDir, opts, ort, &error);
    if (!seg) { serr() << "ERROR: " << error << "\n"; return 1; }
    const qint64 loadMs = clock.restart();

    const auto media = media::FfmpegMediaTool::fromSettings(sm.settings().media);
    // The whole file, without decodeMonoF32's 120 s time limit (a meeting track can be long).
    media::DecodeSpec ds;
    ds.path = audio;
    ds.format = media::PcmFormat{seg->info().sampleRate, 1, media::SampleFormat::F32};
    QVector<float> pcm;
    const media::MediaResult dr = media->decodePcm(ds, [&pcm](const char* data, qsizetype bytes) {
        const qsizetype n = bytes / qsizetype(sizeof(float)), at = pcm.size();
        pcm.resize(at + n);
        std::memcpy(pcm.data() + at, data, size_t(n) * sizeof(float));
    });
    if (!dr.ok() || pcm.isEmpty()) { serr() << "ERROR: could not decode " << audio << ": " << dr.errorText() << "\n"; return 1; }
    const qint64 decodeMs = clock.restart();

    const std::optional<SegmentationResult> r = seg->segment(pcm, {}, nullptr, &error);
    if (!r) { serr() << "ERROR: " << error << "\n"; return 1; }
    const qint64 segMs = clock.restart();
    const double dur = r->durationSec();
    const double rtf = dur > 0 ? segMs / 1000.0 / dur : 0.0;

    // --cluster: the D4 clustering over the same segmentation (one unbound track, file time).
    providers::DiarizeResult clustered;
    TrackClusterStats cstats;
    qint64 clusterMs = 0;
    QStringList usedModels;
    if (cluster) {
        const VoiceEmbedderSet set = LocalDiarizer::withThreads(VoiceEmbedderSet::fromSettings(
            voiceModels.isEmpty() ? sm.settings().processing.voiceModels : voiceModels, sm.paths().modelsDir(), appDir),
            copts.embedThreads);
        if (!set.ensureLoaded()) {
            serr() << "ERROR: no usable voice model for the clustering (" << set.lastError() << ")\n";
            return 1;
        }
        usedModels = set.loadedModelIds();
        providers::DiarizeTrack track;
        track.trackId = QStringLiteral("probe");
        track.path = audio;
        cstats = LocalDiarizer::clusterTrack(track, *r, pcm, opts, copts, usedModels,
                                             [&set, &usedModels](const QVector<float>& p) { return set.embedPcmWith(p, usedModels); },
                                             &clustered);
        clusterMs = clock.elapsed();
    }

    if (json) {
        QJsonObject o;
        o["model"] = r->modelId;
        o["durationSec"] = dur;
        o["windowShiftSamples"] = r->windowShift;
        o["windows"] = int(r->windows.size());
        o["frames"] = r->numFrames;
        o["frameStepSec"] = r->info.frameStepSec();
        o["stitchedSpeakers"] = r->numStitched;
        o["loadMs"] = loadMs;
        o["decodeMs"] = decodeMs;
        o["segmentMs"] = segMs;
        o["rtf"] = rtf;
        o["threads"] = ort.intraOpThreads;
        o["batch"] = opts.batchSize;
        o["executionProviders"] = QJsonArray::fromStringList(onnxExecutionProviders());
        QJsonArray turns;
        for (const segmentation::Turn& t : r->turns)
            turns.append(QJsonObject{{"start", t.start}, {"end", t.end}, {"speaker", t.speaker}, {"overlap", t.overlap}});
        o["turns"] = turns;
        if (cluster) {
            QJsonArray ct, cs;
            for (const providers::SpeakerTurn& t : clustered.turns)
                ct.append(QJsonObject{{"start", t.startMs / 1000.0}, {"end", t.endMs / 1000.0},
                                      {"speaker", t.speakerIndex}, {"overlap", t.overlap}});
            for (const providers::DiarizedSpeaker& sp : clustered.speakers)
                cs.append(QJsonObject{{"index", sp.index}, {"totalMs", sp.totalMs}, {"embeddings", sp.centroid.windows},
                                      {"sampleRef", sp.centroid.sampleRef}});
            o["clusterMs"] = clusterMs;
            o["voiceModels"] = QJsonArray::fromStringList(usedModels);
            o["threshold"] = cstats.threshold;
            o["spans"] = cstats.spans;
            o["embedded"] = cstats.embedded;
            o["subsampled"] = cstats.subsampled;
            o["clusteredTurns"] = ct;
            o["speakers"] = cs;
        }
        if (windows) {
            QJsonArray ws;
            for (const SegmentationWindow& w : r->windows) {
                QJsonArray cls, st;
                for (quint8 c : w.classes) cls.append(int(c));
                for (int s : w.stitched) st.append(s);
                ws.append(QJsonObject{{"startSample", w.startSample}, {"startFrame", w.startFrame},
                                      {"classes", cls}, {"stitched", st}});
            }
            o["windowData"] = ws;
        }
        sout() << QJsonDocument(o).toJson(QJsonDocument::Compact) << "\n";
        sout().flush();
        return 0;
    }

    sout() << "Model:     " << r->modelId << " (" << r->info.modelType << ", " << r->info.framesPerWindow << " frames x "
           << r->info.numClasses << " classes per " << r->info.windowSize / double(r->info.sampleRate) << " s window)\n";
    sout() << "Audio:     " << audio << " (" << QString::number(dur, 'f', 1) << " s)\n";
    sout() << "Windows:   " << r->windows.size() << ", shift " << r->windowShift << " samples ("
           << QString::number(r->windowShift / double(r->info.sampleRate), 'f', 3) << " s)\n";
    sout() << "Timing:    load " << loadMs << " ms, decode " << decodeMs << " ms, segmentation " << segMs
           << " ms (RTF " << QString::number(rtf, 'f', 4) << ", " << ort.intraOpThreads << " thread(s), batch "
           << opts.batchSize << ", " << onnxExecutionProviders().join(", ") << ")\n";
    if (!cluster) {
        sout() << "Speakers:  " << r->numStitched << " stitched local speaker(s) (not clustered; see --cluster)\n";
        sout() << "Turns:     " << r->turns.size() << "\n";
        for (const segmentation::Turn& t : r->turns)
            sout() << "  " << hms(t.start) << " - " << hms(t.end) << "  speaker " << t.speaker
                   << (t.overlap ? "  (overlap)" : "") << "\n";
        sout().flush();
        return 0;
    }
    sout() << "Clustering: " << usedModels.join(", ") << ", threshold " << cstats.threshold
           << (copts.numClusters > 0 ? QStringLiteral(", %1 speakers forced").arg(copts.numClusters) : QString())
           << "; " << cstats.spans << " spans, " << cstats.embedded << " embedded"
           << (cstats.subsampled ? " (subsampled)" : "") << ", " << clusterMs << " ms\n";
    sout() << "Speakers:  " << clustered.speakers.size() << " (from " << r->numStitched << " stitched ids)\n";
    for (const providers::DiarizedSpeaker& sp : clustered.speakers)
        sout() << "  speaker " << sp.index << ": " << QString::number(sp.totalMs / 1000.0, 'f', 1) << " s, "
               << sp.centroid.windows << " embeddings, sample " << sp.centroid.sampleRef << "\n";
    sout() << "Turns:     " << clustered.turns.size() << "\n";
    for (const providers::SpeakerTurn& t : clustered.turns)
        sout() << "  " << hms(t.startMs / 1000.0) << " - " << hms(t.endMs / 1000.0) << "  speaker " << t.speakerIndex
               << (t.overlap ? "  (overlap)" : "") << "\n";
    sout().flush();
    return 0;
}

} // namespace tanara::cli
