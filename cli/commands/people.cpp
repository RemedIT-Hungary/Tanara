// tanara-cli, people module: voiceprints, people list / merge (tag-people lives under `tags`).
// All output is English, no tr().
#include "Commands.h"

#include "tanara/app/AppController.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <algorithm>

namespace tanara::cli {

namespace {

int cmd_voiceprints(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    Q_UNUSED(args)
    auto* vp = app.voiceprints();
    // Two counters: a %n phrase may appear only once, hence two plural phrases.
    out() << QStringLiteral("Voiceprints (%1, %2):")
               .arg(plural(vp->people().size(), "%n person", "%n people"),
                    plural(vp->totalPrintCount(), "%n voiceprint", "%n voiceprints"))
        << "\n";
    for (const PersonId& person : vp->people())
        out() << QStringLiteral("  %1: %2  [%3]").arg(app.people()->displayName(person),
                   plural(vp->printCount(person), "%n voiceprint", "%n voiceprints"), person.toString())
            << "\n";
    out().flush();
    return 0;
}

// A person by PersonId or exact name; null if none (or the name is shared: then the id is needed).
PersonId resolvePerson(library::PeopleService& people, const QString& idOrName, bool* ambiguous)
{
    const QString t = idOrName.trimmed();
    if (t.isEmpty()) return {};
    const PersonId byId = PersonId::fromString(t);
    if (!byId.isNull() && people.exists(byId)) return byId;
    const PersonIds byName = people.findByName(t);
    if (byName.size() > 1) { *ambiguous = true; return {}; }
    return byName.value(0);
}

int peopleUsage()
{
    err() << "Usage: people list | people merge <keep> <drop>   (a person is a PersonId or a unique name)\n";
    return 2;
}

// TODO(P12d): the app facade goes away once the CLI builds library::PeopleService from the services.
int cmd_people(CliContext& ctx, const QStringList& args)
{
    const QString sub = args.value(2);
    if (sub != "list" && sub != "merge") return peopleUsage();
    library::PeopleService* people = ctx.app().peopleService();
    if (!people) { err() << "ERROR: the people service is not available.\n"; return 1; }
    people->reload();
    if (sub == "list") {
        QVector<library::PersonRecord> all = people->persons();
        std::sort(all.begin(), all.end(), [](const library::PersonRecord& a, const library::PersonRecord& b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        out() << "People (" << all.size() << "):\n";
        for (const library::PersonRecord& p : all) {
            out() << "  " << p.name << "  [" << p.id.toString() << "]  "
                  << plural(p.sampleCount, "%n voiceprint", "%n voiceprints");
            if (p.isSelf) out() << "  (you)";
            if (!p.aliases.isEmpty()) out() << "  aka " << p.aliases.join(QStringLiteral(", "));
            out() << "\n";
        }
        out().flush();
        return 0;
    }
    if (args.size() < 5) return peopleUsage();
    bool ambiguous = false;
    const PersonId keep = resolvePerson(*people, args.value(3), &ambiguous);
    const PersonId drop = resolvePerson(*people, args.value(4), &ambiguous);
    if (ambiguous) { err() << "ERROR: a name is shared by several people; use the PersonId (see: people list).\n"; return 1; }
    if (keep.isNull() || drop.isNull()) { err() << "ERROR: no such person.\n"; return 1; }
    if (keep == drop) { err() << "ERROR: both arguments are the same person.\n"; return 1; }
    if (people->isSelf(drop)) { err() << "ERROR: your own person cannot be the one that is dropped; swap the arguments.\n"; return 1; }
    const QString keepName = people->person(keep).name, dropName = people->person(drop).name;
    const library::MergePreview pv = people->mergePreview(drop, keep);
    const library::PeopleOpResult res = people->merge(drop, keep);
    if (!res.ok) { err() << "ERROR: " << res.error << "\n"; return 1; }
    out() << "Merged \"" << dropName << "\" into \"" << keepName << "\" ("
          << plural(res.meetings, "%n meeting", "%n meetings") << ", "
          << plural(res.samples, "%n voiceprint", "%n voiceprints") << ", "
          << plural(pv.staleSummaries, "%n summary", "%n summaries") << " now stale).\n";
    out().flush();
    return 0;
}

} // namespace

void registerPeopleCommands(CommandRegistry& r)
{
    const QString m = QStringLiteral("people");
    r.add({"voiceprints", m, "List the people that have voiceprints", "voiceprints", true, cmd_voiceprints});
    r.add({"people", m, "List the people; merge two of them",
           "people list\npeople merge <keep> <drop>", true, cmd_people});
}

} // namespace tanara::cli
