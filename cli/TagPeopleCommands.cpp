#include "TagPeopleCommands.h"

#include "tanara/AppController.h"
#include "tanara/people/PeopleService.h"
#include "tanara/tags/TagService.h"

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

Tag resolveTag(const TagService& tags, const QString& nameOrId)
{
    const Tag t = tags.tag(nameOrId);
    return t.isValid() ? t : tags.byName(nameOrId);
}

// A tárolt írásmód, ha a személy ismert (különben a bemenet).
QString canonicalPerson(AppController& app, const QString& name)
{
    if (PeopleService* ps = app.peopleService()) {
        ps->reload();
        const PersonRecord r = ps->person(name);
        if (ps->exists(name) && !r.name.isEmpty()) return r.name;
    }
    return name.trimmed();
}

QString counts(const PersonTagStat& s)
{
    return QStringLiteral("%1 / %2").arg(s.shared).arg(s.tagTotal);
}

int usage()
{
    err() << "Usage: tags people <tag> | tags person <name> | tags set-person <name> <tag,...>" << Qt::endl;
    return 1;
}

} // namespace

int runTagPeopleCommand(AppController& app, const QStringList& args)
{
    const QString sub = args.value(2);
    if (sub != QLatin1String("people") && sub != QLatin1String("person") && sub != QLatin1String("set-person"))
        return -1;
    TagService* tags = app.tags();
    if (!tags) return 1;

    if (sub == QLatin1String("people")) {
        if (args.value(3).isEmpty()) return usage();
        const Tag t = resolveTag(*tags, args.value(3));
        if (!t.isValid()) { err() << "ERROR: no such tag." << Qt::endl; return 1; }
        tags->refreshPersonStatsNow();
        const QVector<PersonTagStat> list = tags->peopleStats(t.id, 0);
        out() << QStringLiteral("#%1 — %2 meetings, %3 people").arg(t.name).arg(tags->meetingCount(t.id)).arg(list.size())
              << Qt::endl;
        for (const PersonTagStat& s : list) {
            QStringList flags;
            if (s.manual) flags << QStringLiteral("tagged");
            else flags << QStringLiteral("learned");
            if (s.rejected) flags << QStringLiteral("rejected");
            out() << QStringLiteral("  %1  %2  [%3]").arg(s.name, counts(s), flags.join(QStringLiteral(", ")))
                  << Qt::endl;
        }
        const QVector<PersonTagStat> sugg = tags->suggestPeopleForTag(t.id);
        if (!sugg.isEmpty()) {
            QStringList names;
            for (const PersonTagStat& s : sugg) names << QStringLiteral("+%1 (%2)").arg(s.name, counts(s));
            out() << "Suggested: " << names.join(QStringLiteral(", ")) << Qt::endl;
        }
        return 0;
    }

    const QString name = canonicalPerson(app, args.value(3));
    if (name.isEmpty()) return usage();
    if (tags->isSelf(name)) {
        err() << "ERROR: your own person has no tags (you attend every meeting)." << Qt::endl;
        return 1;
    }

    if (sub == QLatin1String("person")) {
        tags->refreshPersonStatsNow();
        const auto st = tags->personStats();
        const QVector<PersonTagStat> list = tags->personTagStats(name);
        out() << QStringLiteral("%1 — %2 meetings").arg(name).arg(st->personTotal(name)) << Qt::endl;
        for (const PersonTagStat& s : list) {
            QStringList flags;
            if (s.manual) flags << QStringLiteral("tagged");
            if (s.rejected) flags << QStringLiteral("rejected");
            out() << QStringLiteral("  #%1  %2").arg(tags->tag(s.tagId).name, counts(s));
            if (!flags.isEmpty()) out() << QStringLiteral("  [%1]").arg(flags.join(QStringLiteral(", ")));
            out() << Qt::endl;
        }
        const QVector<PersonTagStat> sugg = tags->suggestTagsForPerson(name);
        if (!sugg.isEmpty()) {
            QStringList names;
            for (const PersonTagStat& s : sugg)
                names << QStringLiteral("+#%1 (%2)").arg(tags->tag(s.tagId).name, counts(s));
            out() << "Suggested: " << names.join(QStringLiteral(", ")) << Qt::endl;
        }
        return 0;
    }

    // set-person <name> <tag,...> — a lista a teljes új készlet; üres ("") = minden címke le.
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
    if (!tags->setPersonTags(name, ids)) {
        err() << "ERROR: could not set tags." << Qt::endl;
        return 1;
    }
    QStringList shown;
    for (const QString& id : tags->tagsOfPerson(name)) shown << QStringLiteral("#") + tags->tag(id).name;
    out() << QStringLiteral("%1: %2").arg(name, shown.isEmpty() ? QStringLiteral("(no tags)") : shown.join(QStringLiteral(" ")))
          << Qt::endl;
    return 0;
}

} // namespace tanara::cli
