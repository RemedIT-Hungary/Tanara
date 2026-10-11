// tanara-cli reslice. Minden kimenet angol, tr() nélkül.
#include "ResliceCommand.h"

#include "tanara/edit/TranscriptReslice.h"
#include "tanara/stt/SonioxTiming.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

const QStringList kBuckets{QStringLiteral("0"), QStringLiteral("1-100"), QStringLiteral("101-200"),
                           QStringLiteral("201-300"), QStringLiteral(">300")};

QString verdictName(reslice::Verdict v)
{
    switch (v) {
    case reslice::Verdict::RealWordSpans:    return QStringLiteral("real-word-spans");
    case reslice::Verdict::AlreadyPadded:    return QStringLiteral("already-padded");
    case reslice::Verdict::Reslice:          return QStringLiteral("reslice");
    default:                                 return QStringLiteral("error");
    }
}

QJsonObject planJson(const QString& folder, const reslice::Plan& p, bool dryRun)
{
    QJsonObject o{{"meetingFolder", folder}, {"verdict", verdictName(p.verdict)}, {"dryRun", dryRun},
                  {"tokens", p.tokenCount}, {"medianTokenWidthMs", double(p.medianWidthMs)},
                  {"leadMs", double(sonioxtiming::kLeadMs)}, {"tailMs", double(sonioxtiming::kTailMs)}};
    if (!p.error.isEmpty()) o["error"] = p.error;
    if (p.verdict != reslice::Verdict::Reslice) return o;
    QJsonObject hist;
    for (const QString& b : kBuckets) hist[b] = p.shiftHistogram.value(b);
    o["lines"] = QJsonObject{{"old", int(p.oldLines.size())}, {"new", int(p.newLines.size())},
                             {"renamedIds", p.renamedIds}, {"mergedLines", 0},
                             {"textChanged", p.textChanged}};
    o["startShiftHistogramMs"] = hist;
    o["overlay"] = QJsonObject{{"present", p.overlayPresent}, {"stale", p.overlayStale},
                               {"refs", p.overlayRefs}, {"renamed", p.overlayRenamed}};
    o["summary"] = QJsonObject{{"present", p.summaryPresent}, {"refs", p.summaryRefs},
                               {"renamed", p.summaryRenamed}};
    o["embeddingsCache"] = p.embeddingsPresent;
    return o;
}

void printPlan(const reslice::Plan& p)
{
    out() << "Tokens: " << p.tokenCount << "  median width: " << p.medianWidthMs << " ms\n"
          << "Lines: " << p.oldLines.size() << " -> " << p.newLines.size()
          << "  (ids renamed: " << p.renamedIds << ", merged lines: 0"
          << ", text changed: " << p.textChanged << ")\n"
          << "Start shift (ms earlier):";
    for (const QString& b : kBuckets) out() << "  " << b << ": " << p.shiftHistogram.value(b);
    out() << "\n";
    if (!p.overlayPresent) out() << "Overlay: none\n";
    else if (p.overlayStale) out() << "Overlay: belongs to an older transcript; left untouched\n";
    else out() << "Overlay: " << p.overlayRefs << " id references, " << p.overlayRenamed << " renamed\n";
    if (!p.summaryPresent) out() << "Summary: none\n";
    else out() << "Summary: " << p.summaryRefs << " id references, " << p.summaryRenamed << " renamed\n";
    out() << "Utterance embedding cache: " << (p.embeddingsPresent ? "will be deleted" : "none") << "\n";
}

} // namespace

int runResliceCommand(const QStringList& args)
{
    QString folder;
    bool dryRun = false, json = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == "--dry-run") dryRun = true;
        else if (a == "--json") json = true;
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    if (folder.isEmpty()) {
        err() << "Usage: reslice <meeting-folder> [--dry-run] [--json]\n";
        return 2;
    }
    if (!QFileInfo(folder).isDir()) { err() << "Not a folder: " << folder << "\n"; return 1; }
    folder = QFileInfo(folder).absoluteFilePath();

    const reslice::Plan p = reslice::plan(folder);
    QJsonObject jo = planJson(folder, p, dryRun);
    auto finish = [&](int code) {
        if (json) out() << QJsonDocument(jo).toJson(QJsonDocument::Indented);
        out().flush();
        return code;
    };

    switch (p.verdict) {
    case reslice::Verdict::Error:
        if (!json) err() << "Error: " << p.error << "\n";
        return finish(1);
    case reslice::Verdict::RealWordSpans:
        if (!json) out() << "Median token width " << p.medianWidthMs
                         << " ms: timing looks like real word spans; nothing to do\n";
        return finish(0);
    case reslice::Verdict::AlreadyPadded:
        if (!json) out() << "Lines are already padded (timing: emission); nothing to do\n";
        return finish(0);
    case reslice::Verdict::Reslice:
        break;
    }

    if (!json) printPlan(p);
    if (dryRun) {
        if (!json) out() << "Dry run: nothing written.\n";
        return finish(0);
    }
    const reslice::ApplyResult r = reslice::apply(folder, p);
    jo["applied"] = r.ok;
    jo["backups"] = QJsonArray::fromStringList(r.backups);
    jo["written"] = QJsonArray::fromStringList(r.written);
    jo["embeddingsRemoved"] = r.embeddingsRemoved;
    if (!r.ok) {
        jo["error"] = r.error;
        if (!json) err() << "Error: " << r.error << "\n";
        return finish(1);
    }
    if (!json) {
        for (const QString& b : r.backups) out() << "Backup: " << QFileInfo(b).fileName() << "\n";
        out() << "Written: " << r.written.join(QStringLiteral(", ")) << "\n";
        if (r.embeddingsRemoved) out() << "Deleted: transcript.embeddings.bin\n";
        out() << "Done.\n";
    }
    return finish(0);
}

} // namespace tanara::cli
