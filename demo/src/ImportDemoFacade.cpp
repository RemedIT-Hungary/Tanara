#include "demo/ImportDemoFacade.h"

#include "demo/DemoText.h"
#include "demo/LibraryDemoData.h"

#include <QDate>
#include <QTime>

using tanara::library::ImportScenario;
using tanara::library::ImportScenarioFile;

namespace tanara_demo {

ImportDemoFacade::ImportDemoFacade(QObject* parent) : tanara::library::IImportFacade(parent) {}

tanara::Tag ImportDemoFacade::createTag(const QString& name)
{
    // The ids of the fictional pool; a name outside it gets a made-up id.
    const QString n = name.simplified();
    tanara::Tag t;
    for (const DemoTag& p : tagPool())
        if (p.name.compare(n, Qt::CaseInsensitive) == 0) {
            t.id = p.id;
            t.name = p.name;
        }
    if (t.id.isEmpty()) {
        t.id = QStringLiteral("new-") + n;
        t.name = n;
    }
    return t;
}

std::optional<ImportScenario> ImportDemoFacade::scenario(const QString& state) const
{
    const auto file = [](const QString& path, qint64 ms, int channels, qint64 bytes, bool video = false) {
        ImportScenarioFile r;
        r.path = path;
        r.probed = true;
        r.info.path = path;
        r.info.ok = true;
        r.info.durationMs = ms;
        r.info.channels = channels;
        r.info.sizeBytes = bytes;
        r.info.hasVideo = video;
        r.info.modifiedAt = QDateTime(QDate(2026, 9, 24), QTime(10, 5));
        return r;
    };
    ImportScenario sc;
    if (state.isEmpty())
        return sc;
    const QString dir = demoText("/home/minta/Felvételek/", "/home/sample/Recordings/");
    if (state == QLatin1String("split")) {
        sc.files << file(dir + demoText("Interjú – Varga Nóra.wav", "Interview – Olivia Grant.wav"), 2538000, 2, 447000000);
        sc.files.last().split = true;
        sc.ownTrack = 0;
    } else if (state == QLatin1String("probing")) {
        sc.files << file(dir + demoText("Fókuszcsoport 3 – terem.wav", "Focus group 3 – room.wav"), 4561000, 2, 803000000);
        ImportScenarioFile r;
        r.path = dir + demoText("Fókuszcsoport 3 – moderátor.m4a", "Focus group 3 – moderator.m4a");
        sc.files << r;
    } else if (state == QLatin1String("error")) {
        sc.files << file(dir + demoText("Fókuszcsoport 3 – terem.wav", "Focus group 3 – room.wav"), 4561000, 2, 803000000);
        ImportScenarioFile r;
        r.path = dir + demoText("jegyzetek.pdf", "notes.pdf");
        r.probed = true;
        r.info.path = r.path;
        r.info.error = tr("Nem hang- vagy videófájl (nem olvasható be): %1").arg(demoText("jegyzetek.pdf", "notes.pdf"));
        sc.files << r;
    } else {   // "files", "progress", "failed"
        sc.files << file(dir + demoText("Fókuszcsoport 3 – terem.wav", "Focus group 3 – room.wav"), 4561000, 2, 803000000)
                 << file(dir + demoText("Fókuszcsoport 3 – moderátor.m4a", "Focus group 3 – moderator.m4a"), 4558000, 1, 36400000)
                 << file(dir + demoText("Fókuszcsoport 3 – kamera.mp4", "Focus group 3 – camera.mp4"), 4570000, 2, 1932000000, true);
    }
    // Fictional tag and suggestion.
    if (state == QLatin1String("files") || state == QLatin1String("progress")) {
        sc.tags.append({QStringLiteral("t-kutatas"), demoText("Kutatás", "Research")});
        ImportScenario::Suggestion sug;
        sug.name = demoText("Fókuszcsoportok 2026", "Focus groups 2026");
        sug.id = QStringLiteral("t-fokusz2026");
        sug.source = QStringLiteral("similar");
        sug.reason = tr("hasonló cím: „%1”").arg(demoText("Fókuszcsoport 2", "Focus group 2"));
        sc.suggestion = sug;
    }
    if (state == QLatin1String("failed"))
        sc.error = tr("Nem sikerült beolvasni: %1").arg(demoText("Fókuszcsoport 3 – kamera.mp4", "Focus group 3 – camera.mp4"));
    if (state == QLatin1String("progress")) {
        sc.running = true;
        sc.percent = 42;
        sc.fileIndex = 1;
        sc.fileTotal = 3;
    }
    return sc;
}

} // namespace tanara_demo
