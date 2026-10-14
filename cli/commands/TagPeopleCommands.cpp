#include "TagPeopleCommands.h"
#include "tanara/library/store/PeopleStore.h"

#include "tanara/app/AppController.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/tags/TagService.h"

#include <QTextStream>

namespace tanara::cli {

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

Tag resolveTag(const library::TagService& tags, const QString& nameOrId)
{
    const Tag t = tags.tag(nameOrId);
    return t.isValid() ? t : tags.byName(nameOrId);
}

// The person: a known PersonId, else the first person with that name; null if none.
PersonId findPerson(app::AppController& app, const QString& nameOrId)
{
    library::PeopleStore* people = app.people();
    if (!people) return {};
    people->refresh();
    return people->choiceFor(nameOrId).id;
}

QString counts(const library::PersonTagStat& s)
{
    return QStringLiteral("%1 / %2").arg(s.shared).arg(s.tagTotal);
}

int usage()
{
    err() << "Usage: tags people <tag> | tags person <name|personId> | tags set-person <name|personId> <tag,...>"
          << Qt::endl;
    return 1;
}

} // namespace

int runTagPeopleCommand(app::AppController& app, const QStringList& args)
{
    const QString sub = args.value(2);
    if (sub != QLatin1String("people") && sub != QLatin1String("person") && sub != QLatin1String("set-person"))
        return -1;
    library::TagService* tags = app.tags();
    if (!tags) return 1;

    if (sub == QLatin1String("people")) {
        if (args.value(3).isEmpty()) return usage();
        const Tag t = resolveTag(*tags, args.value(3));
        if (!t.isValid()) { err() << "ERROR: no such tag." << Qt::endl; return 1; }
        tags->refreshPersonStatsNow();
        const QVector<library::PersonTagStat> list = tags->peopleStats(t.id, 0);
        out() << QStringLiteral("#%1 — %2 meetings, %3 people").arg(t.name).arg(tags->meetingCount(t.id)).arg(list.size())
              << Qt::endl;
        for (const library::PersonTagStat& s : list) {
            QStringList flags;
            if (s.manual) flags << QStringLiteral("tagged");
            else flags << QStringLiteral("learned");
            if (s.rejected) flags << QStringLiteral("rejected");
            out() << QStringLiteral("  %1  %2  [%3]").arg(s.name, counts(s), flags.join(QStringLiteral(", ")))
                  << Qt::endl;
        }
        const QVector<library::PersonTagStat> sugg = tags->suggestPeopleForTag(t.id);
        if (!sugg.isEmpty()) {
            QStringList names;
            for (const library::PersonTagStat& s : sugg) names << QStringLiteral("+%1 (%2)").arg(s.name, counts(s));
            out() << "Suggested: " << names.join(QStringLiteral(", ")) << Qt::endl;
        }
        return 0;
    }

    const QString typed = args.value(3).trimmed();
    if (typed.isEmpty()) return usage();
    PersonId person = findPerson(app, typed);
    // set-person on an unknown name creates the person (as the old name-keyed record did).
    if (person.isNull() && sub == QLatin1String("set-person") && app.people()) person = app.people()->create(typed);
    const QString name = person.isNull() ? typed : app.people()->displayName(person);
    if (!person.isNull() && tags->isSelf(person)) {
        err() << "ERROR: your own person has no tags (you attend every meeting)." << Qt::endl;
        return 1;
    }

    if (sub == QLatin1String("person")) {
        tags->refreshPersonStatsNow();
        const auto st = tags->personStats();
        const QVector<library::PersonTagStat> list = tags->personTagStats(person);
        out() << QStringLiteral("%1 — %2 meetings").arg(name).arg(st->personTotal(person)) << Qt::endl;
        for (const library::PersonTagStat& s : list) {
            QStringList flags;
            if (s.manual) flags << QStringLiteral("tagged");
            if (s.rejected) flags << QStringLiteral("rejected");
            out() << QStringLiteral("  #%1  %2").arg(tags->tag(s.tagId).name, counts(s));
            if (!flags.isEmpty()) out() << QStringLiteral("  [%1]").arg(flags.join(QStringLiteral(", ")));
            out() << Qt::endl;
        }
        const QVector<library::PersonTagStat> sugg = tags->suggestTagsForPerson(person);
        if (!sugg.isEmpty()) {
            QStringList names;
            for (const library::PersonTagStat& s : sugg)
                names << QStringLiteral("+#%1 (%2)").arg(tags->tag(s.tagId).name, counts(s));
            out() << "Suggested: " << names.join(QStringLiteral(", ")) << Qt::endl;
        }
        return 0;
    }

    // set-person <name> <tag,...>: the list is the complete new set; empty ("") clears all tags.
    if (args.size() < 5) return usage();
    QStringList ids;
    for (const QString& raw : args.value(4).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString n = raw.trimmed();
        if (n.isEmpty()) continue;
        Tag t = resolveTag(*tags, n);
        if (!t.isValid()) t = tags->create(n);
        if (!t.isValid()) { err() << "ERROR: invalid tag name: " << n << Qt::endl; return 1; }
        if (!ids.contains(t.id)) ids << t.id;
    }
    if (person.isNull() || !tags->setPersonTags(person, ids)) {
        err() << "ERROR: could not set tags." << Qt::endl;
        return 1;
    }
    QStringList shown;
    for (const QString& id : tags->tagsOfPerson(person)) shown << QStringLiteral("#") + tags->tag(id).name;
    out() << QStringLiteral("%1: %2").arg(name, shown.isEmpty() ? QStringLiteral("(no tags)") : shown.join(QStringLiteral(" ")))
          << Qt::endl;
    return 0;
}

} // namespace tanara::cli
